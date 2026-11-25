#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace fairpool {

/**
 * @brief Represents a single concurrent job requesting a bounded resource slot.
 */
struct Job {
    int id = 0;           ///< 1-based original input index (used as FCFS tie-breaker)
    int arrivalTime = 0;  ///< Scheduled arrival tick (T_i >= 0)
    int serviceTime = 0;  ///< Resource hold duration in ticks (W_i >= 0)
    int patienceTime = 0; ///< Maximum wait duration in ticks (P_i >= 0, inclusive)
};

/**
 * @brief Lifecycle event emitted by a job thread during simulation.
 */
enum class EventType {
    Arrived,
    Acquired,
    Completed,
    TimedOut
};

/**
 * @brief Output presentation style for CLI event logging.
 */
enum class OutputStyle {
    Standard, ///< Resource-pool terminology ("Job X arrived", "acquired slot Y", ...)
    Classic   ///< Legacy compatibility format ("Student X arrives", "starts washing", ...)
};

/**
 * @brief Runtime configuration for the FairPool simulation engine.
 */
struct SimulationConfig {
    int numSlots = 1;                       ///< Number of available resource slots (M >= 0)
    int tickMs = 1000;                      ///< Duration of 1 logical tick in milliseconds
    double scaleThreshold = 0.25;           ///< Drop-rate ratio triggering capacity scaling
    bool enableColor = true;                ///< Emit ANSI color escape sequences
    bool jsonOutput = false;                ///< Emit structured JSON summary instead of plain text
    bool quietEvents = false;               ///< Suppress per-event stream lines (useful in benchmarks)
    OutputStyle style = OutputStyle::Standard;
};

/**
 * @brief Per-job execution telemetry captured for analysis and verification.
 */
struct JobOutcome {
    int jobId = 0;
    int arrivalTick = 0;
    int startTick = -1;     ///< Logical tick when resource was acquired (-1 if timed out)
    int finishTick = -1;    ///< Logical tick when resource was released or job timed out
    int waitTicks = 0;      ///< Logical ticks spent waiting in the FCFS queue
    int slotId = -1;        ///< 1-based slot index acquired (-1 if timed out)
    bool served = false;    ///< True if the job acquired a slot and completed
};

/**
 * @brief Aggregate simulation metrics and capacity-scaling recommendation.
 */
struct SimulationStats {
    int totalJobs = 0;
    int totalSlots = 0;
    int servedJobs = 0;
    int droppedJobs = 0;
    double dropRate = 0.0;                  ///< droppedJobs / totalJobs (0.0 when totalJobs == 0)
    double averageWaitTicks = 0.0;          ///< Mean wait time across served jobs
    double peakUtilization = 0.0;           ///< Peak active slots / totalSlots
    double avgUtilization = 0.0;            ///< Total busy slot-ticks / (makespan * totalSlots)
    int makespanTicks = 0;                  ///< Logical completion tick of the last finishing job
    bool scalingRecommended = false;        ///< True when dropRate >= scaleThreshold
    std::vector<JobOutcome> outcomes;       ///< Per-job telemetry sorted by jobId
};

} // namespace fairpool
