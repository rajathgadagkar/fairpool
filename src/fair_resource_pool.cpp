#include "fairpool/fair_resource_pool.hpp"

#include <algorithm>

namespace fairpool {

FairResourcePool::FairResourcePool(int totalSlots)
    : totalSlots_(totalSlots < 0 ? 0 : totalSlots) {
    for (int slot = 1; slot <= totalSlots_; ++slot) {
        freeSlots_.push_back(FreeSlot{slot, 0});
    }
}

void FairResourcePool::insertWaiterOrdered(const std::shared_ptr<Waiter>& waiter) {
    auto it = std::find_if(waitQueue_.begin(), waitQueue_.end(), [&](const auto& existing) {
        if (waiter->arrivalTick != existing->arrivalTick) {
            return waiter->arrivalTick < existing->arrivalTick;
        }
        return waiter->jobId < existing->jobId;
    });
    waitQueue_.insert(it, waiter);
}

bool FairResourcePool::hasPendingReleaseAtOrBeforeLocked(int tick) const {
    for (const auto& entry : activeFinishTicks_) {
        if (entry.second <= tick) {
            return true;
        }
    }
    return false;
}

void FairResourcePool::dispatchNextLocked(int slotId, int currentTick) {
    while (!waitQueue_.empty()) {
        auto next = waitQueue_.front();
        waitQueue_.erase(waitQueue_.begin());

        if (next->cancelled) {
            continue;
        }

        const int startTick = std::max(next->arrivalTick, currentTick);
        next->granted = true;
        next->assignedSlotId = slotId;
        next->acquiredTick = startTick;
        activeFinishTicks_.emplace_back(slotId, startTick + next->serviceTicks);
        ++activeSlots_;
        if (activeSlots_ > peakActiveSlots_) {
            peakActiveSlots_ = activeSlots_;
        }
        cv_.notify_all();
        return;
    }

    // No active waiter — return slot to the free pool with its release tick.
    freeSlots_.push_back(FreeSlot{slotId, currentTick});
    cv_.notify_all();
}

std::optional<AcquisitionGrant> FairResourcePool::tryAcquire(int arrivalTick,
                                                             int serviceTicks,
                                                             int deadlineTick,
                                                             int jobId,
                                                             TimePoint deadlineTp,
                                                             const std::function<void()>& onEntered) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Fast path: if a slot is immediately available and no earlier job is waiting.
    if (!freeSlots_.empty() && waitQueue_.empty()) {
        FreeSlot slot = freeSlots_.front();
        freeSlots_.pop_front();
        const int startTick = std::max(arrivalTick, slot.availableFromTick);
        activeFinishTicks_.emplace_back(slot.slotId, startTick + serviceTicks);
        ++activeSlots_;
        if (activeSlots_ > peakActiveSlots_) {
            peakActiveSlots_ = activeSlots_;
        }
        if (onEntered) {
            onEntered();
        }
        return AcquisitionGrant{slot.slotId, startTick};
    }

    // Slow path: register in strict FCFS order and block without busy-waiting.
    auto waiter = std::make_shared<Waiter>();
    waiter->arrivalTick = arrivalTick;
    waiter->serviceTicks = serviceTicks;
    waiter->jobId = jobId;
    insertWaiterOrdered(waiter);

    if (onEntered) {
        onEntered();
    }

    cv_.wait_until(lock, deadlineTp, [&]() {
        return waiter->granted;
    });

    // Guard against OS timer coalescing: if a running slot is scheduled to
    // release at or before this waiter's inclusive deadlineTick, wait cleanly
    // for that release to settle before declaring a timeout.
    if (!waiter->granted && hasPendingReleaseAtOrBeforeLocked(deadlineTick)) {
        cv_.wait(lock, [&]() {
            return waiter->granted || !hasPendingReleaseAtOrBeforeLocked(deadlineTick);
        });
    }

    if (waiter->granted) {
        return AcquisitionGrant{waiter->assignedSlotId, waiter->acquiredTick};
    }

    // Timed out: mark cancelled and remove from queue if still present.
    waiter->cancelled = true;
    waitQueue_.erase(
        std::remove(waitQueue_.begin(), waitQueue_.end(), waiter),
        waitQueue_.end());

    return std::nullopt;
}

void FairResourcePool::release(int slotId, int releaseTick) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(activeFinishTicks_.begin(), activeFinishTicks_.end(),
                           [slotId](const auto& entry) { return entry.first == slotId; });
    if (it != activeFinishTicks_.end()) {
        activeFinishTicks_.erase(it);
    }
    if (activeSlots_ > 0) {
        --activeSlots_;
    }
    dispatchNextLocked(slotId, releaseTick);
}

int FairResourcePool::peakActiveSlots() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return peakActiveSlots_;
}

} // namespace fairpool
