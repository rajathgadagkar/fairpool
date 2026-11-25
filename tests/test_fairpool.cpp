#include "fairpool/simulator.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int gTestsRun = 0;
int gTestsPassed = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "  [FAIL] " << message << "\n";
        std::exit(1);
    }
}

void runTest(const std::string& name, void (*fn)()) {
    ++gTestsRun;
    std::cout << "[RUN ] " << name << "... " << std::flush;
    fn();
    ++gTestsPassed;
    std::cout << "PASS\n";
}

void testCase1LowDropRate() {
    // 5 jobs, 2 slots -> Job 3 drops at t=8, Job 5 acquires at exact boundary t=10.
    std::vector<fairpool::Job> jobs = {
        {1, 6, 3, 5},
        {2, 3, 4, 3},
        {3, 6, 5, 2},
        {4, 2, 9, 6},
        {5, 8, 5, 2},
    };

    fairpool::SimulationConfig cfg;
    cfg.numSlots = 2;
    cfg.tickMs = 60;
    cfg.enableColor = false;
    cfg.quietEvents = true;

    std::ostringstream oss;
    fairpool::SimulationEngine engine(cfg, jobs, oss);
    auto stats = engine.run();

    expect(stats.totalJobs == 5, "Expected 5 total jobs");
    expect(stats.servedJobs == 4, "Expected 4 served jobs");
    expect(stats.droppedJobs == 1, "Expected 1 dropped job");
    expect(!stats.scalingRecommended, "20% drop rate should not recommend scaling");
    expect(stats.outcomes.size() == 5, "Expected 5 outcomes");

    // Outcomes are sorted by jobId (1..5)
    expect(stats.outcomes[0].served && stats.outcomes[0].startTick == 7 && stats.outcomes[0].finishTick == 10,
           "Job 1 should run [7, 10]");
    expect(stats.outcomes[1].served && stats.outcomes[1].startTick == 3 && stats.outcomes[1].finishTick == 7,
           "Job 2 should run [3, 7]");
    expect(!stats.outcomes[2].served && stats.outcomes[2].finishTick == 8,
           "Job 3 should time out at t=8");
    expect(stats.outcomes[3].served && stats.outcomes[3].startTick == 2 && stats.outcomes[3].finishTick == 11,
           "Job 4 should run [2, 11]");
    expect(stats.outcomes[4].served && stats.outcomes[4].startTick == 10 && stats.outcomes[4].finishTick == 15,
           "Job 5 should acquire at exact boundary t=10 and finish at t=15");
    expect(stats.makespanTicks == 15, "Expected makespan of 15 ticks");
}

void testCase2BoundaryAndHighDropRate() {
    // 3 jobs, 1 slot -> Job 2 runs [1,3], Job 1 acquires at exact boundary t=3, Job 3 drops at t=4.
    std::vector<fairpool::Job> jobs = {
        {1, 2, 5, 1},
        {2, 1, 2, 4},
        {3, 2, 4, 2},
    };

    fairpool::SimulationConfig cfg;
    cfg.numSlots = 1;
    cfg.tickMs = 60;
    cfg.enableColor = false;
    cfg.quietEvents = true;

    std::ostringstream oss;
    fairpool::SimulationEngine engine(cfg, jobs, oss);
    auto stats = engine.run();

    expect(stats.droppedJobs == 1, "Expected 1 dropped job");
    expect(stats.scalingRecommended, "33.3% drop rate should recommend scaling");
    expect(stats.outcomes[0].served && stats.outcomes[0].startTick == 3 && stats.outcomes[0].finishTick == 8,
           "Job 1 should acquire at boundary t=3 and finish at t=8");
    expect(stats.outcomes[1].served && stats.outcomes[1].startTick == 1 && stats.outcomes[1].finishTick == 3,
           "Job 2 should run [1, 3]");
    expect(!stats.outcomes[2].served && stats.outcomes[2].finishTick == 4,
           "Job 3 should time out at t=4");
}

void testSimultaneousArrivalsTieBreaker() {
    // 4 jobs arriving simultaneously at t=0 for 1 slot; lower ID must always go first.
    std::vector<fairpool::Job> jobs = {
        {1, 0, 2, 10},
        {2, 0, 2, 10},
        {3, 0, 2, 10},
        {4, 0, 2, 10},
    };

    fairpool::SimulationConfig cfg;
    cfg.numSlots = 1;
    cfg.tickMs = 50;
    cfg.enableColor = false;
    cfg.quietEvents = true;

    std::ostringstream oss;
    fairpool::SimulationEngine engine(cfg, jobs, oss);
    auto stats = engine.run();

    expect(stats.droppedJobs == 0, "All 4 jobs should be served");
    expect(stats.outcomes[0].startTick == 0 && stats.outcomes[0].finishTick == 2, "Job 1 runs [0,2]");
    expect(stats.outcomes[1].startTick == 2 && stats.outcomes[1].finishTick == 4, "Job 2 runs [2,4]");
    expect(stats.outcomes[2].startTick == 4 && stats.outcomes[2].finishTick == 6, "Job 3 runs [4,6]");
    expect(stats.outcomes[3].startTick == 6 && stats.outcomes[3].finishTick == 8, "Job 4 runs [6,8]");
}

void testZeroPatienceJobs() {
    std::vector<fairpool::Job> jobs = {
        {1, 0, 3, 0}, // Slot free at t=0 -> succeeds
        {2, 1, 2, 0}, // Slot busy at t=1, patience 0 -> drops immediately
        {3, 3, 1, 0}, // Slot freed at t=3, patience 0 -> succeeds at boundary t=3
    };

    fairpool::SimulationConfig cfg;
    cfg.numSlots = 1;
    cfg.tickMs = 60;
    cfg.enableColor = false;
    cfg.quietEvents = true;

    std::ostringstream oss;
    fairpool::SimulationEngine engine(cfg, jobs, oss);
    auto stats = engine.run();

    expect(stats.outcomes[0].served, "Job 1 should succeed with P=0 when slot is free");
    expect(!stats.outcomes[1].served, "Job 2 should drop with P=0 when slot is busy");
    expect(stats.outcomes[2].served, "Job 3 should succeed at exact release tick t=3");
    expect(stats.droppedJobs == 1, "Expected exactly 1 dropped job");
}

void testZeroSlotsEdgeCase() {
    std::vector<fairpool::Job> jobs = {
        {1, 0, 2, 1},
        {2, 1, 2, 1},
    };

    fairpool::SimulationConfig cfg;
    cfg.numSlots = 0;
    cfg.tickMs = 50;
    cfg.enableColor = false;
    cfg.quietEvents = true;

    std::ostringstream oss;
    fairpool::SimulationEngine engine(cfg, jobs, oss);
    auto stats = engine.run();

    expect(stats.servedJobs == 0, "0 slots should serve 0 jobs");
    expect(stats.droppedJobs == 2, "All jobs should time out with 0 slots");
    expect(stats.scalingRecommended, "100% drop rate should recommend scaling");
}

void testEmptyWorkload() {
    std::vector<fairpool::Job> jobs;
    fairpool::SimulationConfig cfg;
    cfg.numSlots = 2;
    cfg.tickMs = 50;
    cfg.enableColor = false;
    cfg.quietEvents = true;

    std::ostringstream oss;
    fairpool::SimulationEngine engine(cfg, jobs, oss);
    auto stats = engine.run();

    expect(stats.totalJobs == 0, "Expected 0 total jobs");
    expect(stats.droppedJobs == 0, "Expected 0 dropped jobs");
    expect(!stats.scalingRecommended, "Empty workload should not recommend scaling");
}

void testClassicOutputCompatibility() {
    std::vector<fairpool::Job> jobs = {
        {1, 2, 5, 1},
        {2, 1, 2, 4},
        {3, 2, 4, 2},
    };

    fairpool::SimulationConfig cfg;
    cfg.numSlots = 1;
    cfg.tickMs = 50;
    cfg.enableColor = false;
    cfg.style = fairpool::OutputStyle::Classic;

    std::ostringstream oss;
    fairpool::SimulationEngine engine(cfg, jobs, oss);
    engine.run();

    std::string out = oss.str();
    expect(out.find("Student 2 arrives\n") != std::string::npos, "Missing Student 2 arrival");
    expect(out.find("Student 2 starts washing\n") != std::string::npos, "Missing Student 2 start");
    expect(out.find("Student 1 starts washing\n") != std::string::npos, "Missing Student 1 start");
    expect(out.find("Student 3 leaves without washing\n") != std::string::npos, "Missing Student 3 timeout");
    expect(out.find("1\nYes\n") != std::string::npos, "Missing final summary '1\\nYes'");
}

void testJsonOutputFormat() {
    std::vector<fairpool::Job> jobs = {
        {1, 0, 2, 2},
        {2, 0, 2, 1},
    };

    fairpool::SimulationConfig cfg;
    cfg.numSlots = 1;
    cfg.tickMs = 50;
    cfg.enableColor = false;
    cfg.jsonOutput = true;

    std::ostringstream oss;
    fairpool::SimulationEngine engine(cfg, jobs, oss);
    engine.run();

    std::string out = oss.str();
    expect(out.find("\"total_jobs\": 2") != std::string::npos, "JSON missing total_jobs");
    expect(out.find("\"dropped_jobs\": 1") != std::string::npos, "JSON missing dropped_jobs");
    expect(out.find("\"scaling_recommended\": true") != std::string::npos, "JSON missing scaling_recommended");
}

void testConcurrencyStress() {
    // 24 concurrent threads competing for 4 slots across staggered & simultaneous waves.
    std::vector<fairpool::Job> jobs;
    for (int i = 1; i <= 24; ++i) {
        int arrival = (i - 1) / 4; // 6 waves of 4 simultaneous jobs
        int service = 2 + (i % 3); // 2..4 ticks
        int patience = 3 + (i % 4); // 3..6 ticks
        jobs.push_back({i, arrival, service, patience});
    }

    fairpool::SimulationConfig cfg;
    cfg.numSlots = 4;
    cfg.tickMs = 50;
    cfg.enableColor = false;
    cfg.quietEvents = true;

    std::ostringstream oss;
    fairpool::SimulationEngine engine(cfg, jobs, oss);
    auto stats = engine.run();

    expect(stats.totalJobs == 24, "Expected 24 jobs");
    expect(stats.servedJobs + stats.droppedJobs == 24, "served + dropped must equal total");
    expect(stats.peakUtilization <= 1.0 + 1e-9, "Peak utilization cannot exceed 100%");

    for (const auto& o : stats.outcomes) {
        if (o.served) {
            expect(o.slotId >= 1 && o.slotId <= 4, "Slot ID must be in [1, 4]");
            expect(o.startTick >= o.arrivalTick, "Start tick cannot precede arrival tick");
            expect(o.waitTicks <= jobs[static_cast<std::size_t>(o.jobId - 1)].patienceTime,
                   "Served job wait time must not exceed patience");
        }
    }
}

} // namespace

int main() {
    std::cout << "=== FairPool Unit & Concurrency Test Suite ===\n";
    runTest("Case 1: Low Drop Rate & Boundary Grant", testCase1LowDropRate);
    runTest("Case 2: High Drop Rate & Exact Boundary", testCase2BoundaryAndHighDropRate);
    runTest("Simultaneous Arrivals FCFS Tie-Breaker", testSimultaneousArrivalsTieBreaker);
    runTest("Zero-Patience Acquisition & Timeout", testZeroPatienceJobs);
    runTest("Zero-Slot Pool Edge Case", testZeroSlotsEdgeCase);
    runTest("Empty Workload Edge Case", testEmptyWorkload);
    runTest("Classic Output Compatibility", testClassicOutputCompatibility);
    runTest("Structured JSON Telemetry Output", testJsonOutputFormat);
    runTest("24-Thread Concurrency Stress Test", testConcurrencyStress);

    std::cout << "=== Passed " << gTestsPassed << "/" << gTestsRun << " tests ===\n";
    return 0;
}
