#include "fairpool/logger.hpp"

#include <iomanip>

namespace fairpool {

namespace {
constexpr const char* kColorWhite  = "\033[37m";
constexpr const char* kColorGreen  = "\033[32m";
constexpr const char* kColorYellow = "\033[33m";
constexpr const char* kColorRed    = "\033[31m";
constexpr const char* kColorReset  = "\033[0m";
} // namespace

EventLogger::EventLogger(std::ostream& out,
                         bool enableColor,
                         OutputStyle style,
                         bool quietEvents)
    : out_(out),
      enableColor_(enableColor),
      style_(style),
      quietEvents_(quietEvents) {}

const char* EventLogger::colorFor(EventType type) const {
    if (!enableColor_) {
        return "";
    }
    switch (type) {
        case EventType::Arrived:   return kColorWhite;
        case EventType::Acquired:  return kColorGreen;
        case EventType::Completed: return kColorYellow;
        case EventType::TimedOut:  return kColorRed;
    }
    return "";
}

const char* EventLogger::resetCode() const {
    return enableColor_ ? kColorReset : "";
}

void EventLogger::logEvent(int jobId, EventType type, int slotId, int tick) {
    if (quietEvents_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    out_ << colorFor(type);

    if (style_ == OutputStyle::Classic) {
        switch (type) {
            case EventType::Arrived:
                out_ << "Student " << jobId << " arrives\n";
                break;
            case EventType::Acquired:
                out_ << "Student " << jobId << " starts washing\n";
                break;
            case EventType::Completed:
                out_ << "Student " << jobId << " leaves after washing\n";
                break;
            case EventType::TimedOut:
                out_ << "Student " << jobId << " leaves without washing\n";
                break;
        }
    } else {
        if (tick >= 0) {
            out_ << "[t=" << std::setw(3) << tick << "] ";
        }
        switch (type) {
            case EventType::Arrived:
                out_ << "Job " << jobId << " arrived\n";
                break;
            case EventType::Acquired:
                out_ << "Job " << jobId << " acquired slot #" << slotId << "\n";
                break;
            case EventType::Completed:
                out_ << "Job " << jobId << " released slot #" << slotId << " (completed)\n";
                break;
            case EventType::TimedOut:
                out_ << "Job " << jobId << " timed out (patience exceeded)\n";
                break;
        }
    }

    out_ << resetCode() << std::flush;
}

void EventLogger::logSummary(const SimulationStats& stats, bool jsonOutput) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (jsonOutput) {
        out_ << std::fixed << std::setprecision(4);
        out_ << "{\n"
             << "  \"total_jobs\": " << stats.totalJobs << ",\n"
             << "  \"total_slots\": " << stats.totalSlots << ",\n"
             << "  \"served_jobs\": " << stats.servedJobs << ",\n"
             << "  \"dropped_jobs\": " << stats.droppedJobs << ",\n"
             << "  \"drop_rate\": " << stats.dropRate << ",\n"
             << "  \"average_wait_ticks\": " << stats.averageWaitTicks << ",\n"
             << "  \"peak_utilization\": " << stats.peakUtilization << ",\n"
             << "  \"average_utilization\": " << stats.avgUtilization << ",\n"
             << "  \"makespan_ticks\": " << stats.makespanTicks << ",\n"
             << "  \"scaling_recommended\": " << (stats.scalingRecommended ? "true" : "false") << ",\n"
             << "  \"outcomes\": [\n";

        for (std::size_t i = 0; i < stats.outcomes.size(); ++i) {
            const auto& o = stats.outcomes[i];
            out_ << "    {"
                 << "\"job_id\": " << o.jobId
                 << ", \"arrival_tick\": " << o.arrivalTick
                 << ", \"start_tick\": " << o.startTick
                 << ", \"finish_tick\": " << o.finishTick
                 << ", \"wait_ticks\": " << o.waitTicks
                 << ", \"slot_id\": " << o.slotId
                 << ", \"served\": " << (o.served ? "true" : "false")
                 << "}";
            if (i + 1 < stats.outcomes.size()) {
                out_ << ",";
            }
            out_ << "\n";
        }
        out_ << "  ]\n"
             << "}\n"
             << std::flush;
        return;
    }

    out_ << stats.droppedJobs << "\n";
    out_ << (enableColor_ ? kColorWhite : "")
         << (stats.scalingRecommended ? "Yes\n" : "No\n")
         << resetCode() << std::flush;
}

} // namespace fairpool
