#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace fairpool {

/**
 * @brief Result returned when a worker successfully acquires a resource slot.
 */
struct AcquisitionGrant {
    int slotId = -1;       ///< 1-based slot ID assigned to the worker
    int acquiredTick = 0;  ///< Logical simulation tick when the slot was granted
};

/**
 * @brief Strict FCFS bounded-resource pool with non-busy-waiting patience timeouts.
 *
 * Unlike a raw counting semaphore—whose OS wakeup order is non-deterministic—
 * FairResourcePool enforces strict First-Come, First-Served ordering keyed by
 * (arrivalTime, jobId) with deterministic lower-ID tie-breaking.
 *
 * Key concurrency properties:
 * - Zero busy-waiting: waiting threads block on a condition variable until
 *   either granted a slot or their inclusive patience deadline expires.
 * - Deadlock-free: single-mutex lock discipline with RAII scoped locks.
 * - Deterministic inclusive deadline: workers whose patience expires at the
 *   exact tick a slot is released (t_release == t_arrival + patience) reliably
 *   acquire the slot via a calibrated boundary grace window.
 */
class FairResourcePool {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    /**
     * @brief Constructs a pool with a fixed capacity of resource slots.
     *
     * @param totalSlots Number of concurrent resource slots (M >= 0).
     */
    explicit FairResourcePool(int totalSlots);

    /**
     * @brief Attempts to acquire a slot in strict FCFS order before the deadline.
     *
     * @param arrivalTick   Logical arrival tick of the requesting job.
     * @param jobId         1-based job ID used as FCFS tie-breaker.
     * @param deadlineTp    Wall-clock deadline (including boundary grace).
     * @return AcquisitionGrant if a slot was acquired, or std::nullopt on timeout.
     */
    std::optional<AcquisitionGrant> tryAcquire(int arrivalTick,
                                               int jobId,
                                               TimePoint deadlineTp);

    /**
     * @brief Releases a previously acquired slot and wakes the next FCFS waiter.
     *
     * @param slotId      1-based slot ID being returned to the pool.
     * @param releaseTick Logical simulation tick at which the slot became free.
     */
    void release(int slotId, int releaseTick);

    /**
     * @brief Returns the peak number of simultaneously active slots observed.
     */
    int peakActiveSlots() const;

    /**
     * @brief Returns total slot capacity configured for the pool.
     */
    int capacity() const { return totalSlots_; }

private:
    struct Waiter {
        int arrivalTick = 0;
        int jobId = 0;
        bool granted = false;
        bool cancelled = false;
        int assignedSlotId = -1;
        int acquiredTick = 0;
    };

    void insertWaiterOrdered(const std::shared_ptr<Waiter>& waiter);
    void dispatchNextLocked(int slotId, int currentTick);

    int totalSlots_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<int> freeSlots_;
    std::vector<std::shared_ptr<Waiter>> waitQueue_;
    int activeSlots_ = 0;
    int peakActiveSlots_ = 0;
};

} // namespace fairpool
