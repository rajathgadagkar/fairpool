#pragma once

#include "fairpool/fair_resource_pool.hpp"
#include "fairpool/logger.hpp"
#include "fairpool/types.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <ostream>
#include <thread>
#include <vector>

namespace fairpool {

/**
 * @brief Multithreaded FCFS bounded-resource simulation engine.
 *
 * Orchestrates concurrent worker threads using a drift-free steady_clock
 * schedule, coordinates simultaneous arrival waves in ascending job ID order,
 * manages resource allocation through FairResourcePool, and collects per-job
 * and aggregate telemetry.
 */
class SimulationEngine {
public:
    /**
     * @brief Constructs a simulation engine with explicit configuration and jobs.
     *
     * @param config Simulation parameters (slots, tick duration, threshold, style).
     * @param jobs   List of jobs to simulate (will be sorted internally by arrival/ID).
     * @param out    Output stream for event logs and summary (defaults to std::cout).
     */
    SimulationEngine(SimulationConfig config,
                     std::vector<Job> jobs,
                     std::ostream& out = std::cout);

    /**
     * @brief Executes the full multithreaded simulation and returns telemetry.
     */
    SimulationStats run();

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    void sortJobsByFcfs();
    TimePoint tickToTimePoint(int tick) const;
    std::chrono::milliseconds boundaryGrace() const;
    void workerTask(const Job& job,
                    std::shared_ptr<std::mutex> entryMtx,
                    std::shared_ptr<std::condition_variable> entryCv,
                    std::shared_ptr<bool> enteredPool);

    SimulationConfig config_;
    std::vector<Job> jobs_;
    FairResourcePool pool_;
    EventLogger logger_;

    TimePoint simStart_;
    std::atomic<int> droppedCount_{0};
    std::mutex outcomesMutex_;
    std::vector<JobOutcome> outcomes_;
};

} // namespace fairpool
