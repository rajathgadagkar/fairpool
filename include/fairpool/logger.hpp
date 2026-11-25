#pragma once

#include "fairpool/types.hpp"

#include <iostream>
#include <mutex>
#include <ostream>
#include <string>

namespace fairpool {

/**
 * @brief Thread-safe ANSI-colored event and summary logger.
 *
 * Serializes all concurrent stdout writes through an internal mutex so
 * multi-threaded event lines never interleave.
 */
class EventLogger {
public:
    explicit EventLogger(std::ostream& out,
                         bool enableColor = true,
                         OutputStyle style = OutputStyle::Standard,
                         bool quietEvents = false);

    /**
     * @brief Logs a single job lifecycle event in a thread-safe manner.
     *
     * @param jobId  1-based job identifier.
     * @param type   Lifecycle event type.
     * @param slotId 1-based slot identifier (used for Acquired/Completed in Standard style).
     * @param tick   Logical simulation tick (optional display metadata).
     */
    void logEvent(int jobId, EventType type, int slotId = -1, int tick = -1);

    /**
     * @brief Writes the final drop count and capacity scaling recommendation.
     */
    void logSummary(const SimulationStats& stats, bool jsonOutput = false);

private:
    const char* colorFor(EventType type) const;
    const char* resetCode() const;

    std::ostream& out_;
    bool enableColor_;
    OutputStyle style_;
    bool quietEvents_;
    std::mutex mutex_;
};

} // namespace fairpool
