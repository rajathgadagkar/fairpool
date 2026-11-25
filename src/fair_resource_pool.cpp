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

void FairResourcePool::dispatchNextLocked(int slotId, int currentTick) {
    while (!waitQueue_.empty()) {
        auto next = waitQueue_.front();
        waitQueue_.erase(waitQueue_.begin());

        if (next->cancelled) {
            continue;
        }

        next->granted = true;
        next->assignedSlotId = slotId;
        next->acquiredTick = currentTick;
        ++activeSlots_;
        if (activeSlots_ > peakActiveSlots_) {
            peakActiveSlots_ = activeSlots_;
        }
        cv_.notify_all();
        return;
    }

    // No active waiter — return slot to the free pool with its release tick.
    freeSlots_.push_back(FreeSlot{slotId, currentTick});
}

std::optional<AcquisitionGrant> FairResourcePool::tryAcquire(int arrivalTick,
                                                             int jobId,
                                                             TimePoint deadlineTp,
                                                             const std::function<void()>& onEntered) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Fast path: if a slot is immediately available and no earlier job is waiting.
    if (!freeSlots_.empty() && waitQueue_.empty()) {
        FreeSlot slot = freeSlots_.front();
        freeSlots_.pop_front();
        ++activeSlots_;
        if (activeSlots_ > peakActiveSlots_) {
            peakActiveSlots_ = activeSlots_;
        }
        if (onEntered) {
            onEntered();
        }
        return AcquisitionGrant{slot.slotId, std::max(arrivalTick, slot.availableFromTick)};
    }

    // Slow path: register in strict FCFS order and block without busy-waiting.
    auto waiter = std::make_shared<Waiter>();
    waiter->arrivalTick = arrivalTick;
    waiter->jobId = jobId;
    insertWaiterOrdered(waiter);

    if (onEntered) {
        onEntered();
    }

    cv_.wait_until(lock, deadlineTp, [&]() {
        return waiter->granted;
    });

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
