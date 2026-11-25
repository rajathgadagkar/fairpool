#include "fairpool/simulator.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage(const char* prog) {
    std::cout
        << "FairPool — Multithreaded FCFS Resource Pool & Patience-Aware Concurrency Simulator\n"
        << "Usage: " << prog << " [OPTIONS]\n\n"
        << "Input (stdin or --input <file>):\n"
        << "  First line:  N M          (number of jobs, number of resource slots)\n"
        << "  Next N lines: T_i W_i P_i (arrival tick, service ticks, patience ticks)\n\n"
        << "Options:\n"
        << "  --input <path>      Read workload from file instead of stdin\n"
        << "  --tick-ms <ms>      Milliseconds per logical tick (default: 1000)\n"
        << "  --threshold <0..1>  Drop-rate threshold for scaling recommendation (default: 0.25)\n"
        << "  --classic           Use legacy event log wording (Student/washing format)\n"
        << "  --no-color          Disable ANSI color escape codes\n"
        << "  --json              Output structured JSON telemetry summary\n"
        << "  --quiet             Suppress per-event log lines\n"
        << "  -h, --help          Show this help message\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    fairpool::SimulationConfig config;
    std::string inputPath;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--input" && i + 1 < argc) {
            inputPath = argv[++i];
        } else if (arg == "--tick-ms" && i + 1 < argc) {
            config.tickMs = std::atoi(argv[++i]);
            if (config.tickMs <= 0) {
                std::cerr << "Error: --tick-ms must be a positive integer.\n";
                return 1;
            }
        } else if (arg == "--threshold" && i + 1 < argc) {
            config.scaleThreshold = std::atof(argv[++i]);
            if (config.scaleThreshold < 0.0 || config.scaleThreshold > 1.0) {
                std::cerr << "Error: --threshold must be between 0.0 and 1.0.\n";
                return 1;
            }
        } else if (arg == "--classic") {
            config.style = fairpool::OutputStyle::Classic;
        } else if (arg == "--no-color") {
            config.enableColor = false;
        } else if (arg == "--json") {
            config.jsonOutput = true;
            config.enableColor = false;
        } else if (arg == "--quiet") {
            config.quietEvents = true;
        } else {
            std::cerr << "Error: Unknown or incomplete option '" << arg << "'. Use --help for usage.\n";
            return 1;
        }
    }

    std::ifstream fileStream;
    std::istream* in = &std::cin;
    if (!inputPath.empty()) {
        fileStream.open(inputPath);
        if (!fileStream.is_open()) {
            std::cerr << "Error: Could not open input file '" << inputPath << "'.\n";
            return 1;
        }
        in = &fileStream;
    }

    int numJobs = 0;
    int numSlots = 0;
    if (!(*in >> numJobs >> numSlots) || numJobs < 0 || numSlots < 0) {
        std::cerr << "Error: Expected two non-negative integers (N M) on the first line.\n";
        return 1;
    }

    std::vector<fairpool::Job> jobs;
    jobs.reserve(static_cast<std::size_t>(numJobs));
    for (int i = 0; i < numJobs; ++i) {
        int arrival = 0;
        int service = 0;
        int patience = 0;
        if (!(*in >> arrival >> service >> patience) || arrival < 0 || service < 0 || patience < 0) {
            std::cerr << "Error: Invalid job specification on line " << (i + 2)
                      << " (expected three non-negative integers T W P).\n";
            return 1;
        }
        jobs.push_back(fairpool::Job{i + 1, arrival, service, patience});
    }

    config.numSlots = numSlots;
    fairpool::SimulationEngine engine(config, std::move(jobs), std::cout);
    engine.run();

    return 0;
}
