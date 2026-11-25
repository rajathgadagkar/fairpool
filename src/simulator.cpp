#include "fairpool/simulator.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fairpool {

SimulationEngine::SimulationEngine(SimulationConfig config,
                                   std::vector<Job> jobs,
                                   std::ostream& out)
    : config_(config),
      jobs_(std::move(jobs)),
      pool_(config.numSlots),
      logger_(out, config.enableColor, config.style, config.quietEvents || config.jsonOutput) {}

void SimulationEngine::sortJobsByFcfs() {
    std::sort(jobs_.begin(), jobs_.end(), [](const Job& a, const Job& b) {
        if (a.arrivalTime != b.arrivalTime) {
            return a.arrivalTime < b.arrivalTime;
        }
        return a.id < b.id;
    });
}

SimulationEngine::TimePoint SimulationEngine::tickToTimePoint(int tick) const {
    return simStart_ + std::chrono::milliseconds(static_cast<long long>(tick) * config_.tickMs);
}

std::chrono::milliseconds SimulationEngine::boundaryGrace() const {
    // Allow releases scheduled at exact tick (t_arrival + patience) to complete
    // before the waiting thread times out, even under VM timer coalescing,
    // without overlapping tick (t + 1).
    long long third = std::max<long long>(4, config_.tickMs / 3);
    long long capped = std::min<long long>(third, 200);
    return std::chrono::milliseconds(capped);
}

void SimulationEngine::workerTask(const Job& job,
                                  std::shared_ptr<std::mutex> entryMtx,
                                  std::shared_ptr<std::condition_variable> entryCv,
                                  std::shared_ptr<bool> enteredPool) {
    logger_.logEvent(job.id, EventType::Arrived, -1, job.arrivalTime);

    const int deadlineTick = job.arrivalTime + job.patienceTime;
    const TimePoint deadlineTp = tickToTimePoint(deadlineTick) + boundaryGrace();

    // Signal the orchestrator inside FairResourcePool::tryAcquire once this job
    // has acquired a slot or entered the FCFS wait queue.
    auto signalEntered = [&]() {
        {
            std::lock_guard<std::mutex> lk(*entryMtx);
            *enteredPool = true;
        }
        entryCv->notify_one();
    };

    auto grant = pool_.tryAcquire(job.arrivalTime, job.id, deadlineTp, signalEntered);

    JobOutcome outcome;
    outcome.jobId = job.id;
    outcome.arrivalTick = job.arrivalTime;

    if (grant.has_value()) {
        const int slotId = grant->slotId;
        const int startTick = std::max(job.arrivalTime, grant->acquiredTick);
        const int finishTick = startTick + job.serviceTime;

        logger_.logEvent(job.id, EventType::Acquired, slotId, startTick);

        if (job.serviceTime > 0) {
            std::this_thread::sleep_until(tickToTimePoint(finishTick));
        }

        logger_.logEvent(job.id, EventType::Completed, slotId, finishTick);
        pool_.release(slotId, finishTick);

        outcome.served = true;
        outcome.slotId = slotId;
        outcome.startTick = startTick;
        outcome.finishTick = finishTick;
        outcome.waitTicks = startTick - job.arrivalTime;
    } else {
        logger_.logEvent(job.id, EventType::TimedOut, -1, deadlineTick);
        droppedCount_.fetch_add(1, std::memory_order_relaxed);

        outcome.served = false;
        outcome.slotId = -1;
        outcome.startTick = -1;
        outcome.finishTick = deadlineTick;
        outcome.waitTicks = job.patienceTime;
    }

    std::lock_guard<std::mutex> lk(outcomesMutex_);
    outcomes_.push_back(outcome);
}

SimulationStats SimulationEngine::run() {
    sortJobsByFcfs();
    outcomes_.clear();
    outcomes_.reserve(jobs_.size());
    droppedCount_.store(0, std::memory_order_relaxed);

    simStart_ = Clock::now();
    std::vector<std::thread> threads;
    threads.reserve(jobs_.size());

    for (std::size_t i = 0; i < jobs_.size(); ++i) {
        const Job& job = jobs_[i];
        if (job.arrivalTime > 0) {
            std::this_thread::sleep_until(tickToTimePoint(job.arrivalTime));
        }

        auto entryMtx = std::make_shared<std::mutex>();
        auto entryCv = std::make_shared<std::condition_variable>();
        auto enteredPool = std::make_shared<bool>(false);

        threads.emplace_back(&SimulationEngine::workerTask,
                             this,
                             job,
                             entryMtx,
                             entryCv,
                             enteredPool);

        // Wait until this worker has registered in FairResourcePool before
        // advancing to a same-tick peer, guaranteeing strict FCFS order.
        bool nextSameTick = (i + 1 < jobs_.size() && jobs_[i + 1].arrivalTime == job.arrivalTime);
        if (nextSameTick) {
            std::unique_lock<std::mutex> lk(*entryMtx);
            entryCv->wait(lk, [&]() { return *enteredPool; });
        }
    }

    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    std::sort(outcomes_.begin(), outcomes_.end(), [](const JobOutcome& a, const JobOutcome& b) {
        return a.jobId < b.jobId;
    });

    SimulationStats stats;
    stats.totalJobs = static_cast<int>(jobs_.size());
    stats.totalSlots = config_.numSlots;
    stats.droppedJobs = droppedCount_.load(std::memory_order_relaxed);
    stats.servedJobs = stats.totalJobs - stats.droppedJobs;
    stats.dropRate = (stats.totalJobs > 0)
                         ? static_cast<double>(stats.droppedJobs) / static_cast<double>(stats.totalJobs)
                         : 0.0;
    stats.scalingRecommended = (stats.totalJobs > 0) && (stats.dropRate >= config_.scaleThreshold);

    int totalWaitServed = 0;
    int totalBusySlotTicks = 0;
    int maxFinishTick = 0;

    for (const auto& o : outcomes_) {
        if (o.finishTick > maxFinishTick) {
            maxFinishTick = o.finishTick;
        }
        if (o.served) {
            totalWaitServed += o.waitTicks;
            totalBusySlotTicks += (o.finishTick - o.startTick);
        }
    }

    stats.makespanTicks = maxFinishTick;
    stats.averageWaitTicks = (stats.servedJobs > 0)
                                 ? static_cast<double>(totalWaitServed) / static_cast<double>(stats.servedJobs)
                                 : 0.0;
    stats.peakUtilization = (stats.totalSlots > 0)
                                ? static_cast<double>(pool_.peakActiveSlots()) / static_cast<double>(stats.totalSlots)
                                : 0.0;
    stats.avgUtilization = (stats.totalSlots > 0 && stats.makespanTicks > 0)
                               ? static_cast<double>(totalBusySlotTicks) /
                                     (static_cast<double>(stats.makespanTicks) * static_cast<double>(stats.totalSlots))
                               : 0.0;
    stats.outcomes = outcomes_;

    logger_.logSummary(stats, config_.jsonOutput);
    return stats;
}

} // namespace fairpool
