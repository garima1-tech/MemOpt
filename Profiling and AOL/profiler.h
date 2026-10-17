// profiler.h - Modular profiling layer.
//
// Two separate things happen here:
//
// 1. IProfiler implementations collect PROCESS-LEVEL counters while a
//    workload runs (cycles, instructions, cache misses, page faults ...).
//    LinuxPerfProfiler reads real counters through perf_event_open() when
//    the kernel, CPU and permissions allow it. FallbackProfiler collects
//    nothing and says so.
//
// 2. UnitMetricEstimator fills the PER-DATA-UNIT latency and MLP inputs
//    of the AOL model. No profiler in this prototype can measure per-unit
//    memory latency (that needs Intel PEBS / AMD IBS style sampling with
//    address attribution), so in "fallback" mode these values are:
//        latency -> SYNTHETIC (assumed base latency, labelled)
//        MLP     -> ESTIMATED (heuristic from the measured access pattern)
//    In "measured" mode they are reported UNAVAILABLE instead.
//    Query duration is NEVER used as memory latency.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "config.h"

namespace memopt {

struct ProfileReport {
    std::string profiler;        // "fallback", "linux-perf"
    std::string scope;           // what process the counters describe
    bool hardware_counters = false;  // at least one HW counter measured
    std::vector<Metric> counters;
    std::string status;
};

class IProfiler {
public:
    virtual ~IProfiler() = default;
    virtual std::string name() const = 0;
    // Can this profiler collect anything at all on this machine?
    virtual bool available(std::string& why) const = 0;
    // target_pid: process to observe (e.g. the PostgreSQL backend). 0 = self.
    virtual bool start(int target_pid) = 0;
    virtual ProfileReport stop() = 0;
    // True only if the profiler can attribute latency/MLP to data units.
    virtual bool providesPerUnitLatency() const { return false; }
};

class FallbackProfiler : public IProfiler {
public:
    std::string name() const override { return "fallback"; }
    bool available(std::string& why) const override;
    bool start(int target_pid) override;
    ProfileReport stop() override;
};

// Real counters via perf_event_open (Linux / WSL2 only). On other
// platforms it compiles but reports itself unavailable.
class LinuxPerfProfiler : public IProfiler {
public:
    LinuxPerfProfiler();
    ~LinuxPerfProfiler() override;
    std::string name() const override { return "linux-perf"; }
    bool available(std::string& why) const override;
    bool start(int target_pid) override;
    ProfileReport stop() override;

private:
    struct Counter;
    std::vector<Counter*> counters_;
    std::string scope_;
    std::string start_note_;
    void closeAll();
};

// Picks a profiler according to cfg.profiler ("auto" tries linux-perf
// first, then fallback). `note` explains the choice.
std::unique_ptr<IProfiler> createProfiler(const AppConfig& cfg, std::string& note);

class UnitMetricEstimator {
public:
    explicit UnitMetricEstimator(const AppConfig& cfg) : cfg_(cfg) {}
    // Fills DataUnit::latency and DataUnit::mlp for every unit.
    void estimate(std::vector<DataUnit>& units, const IProfiler& profiler) const;

    // Exposed for unit tests.
    double syntheticLatencyFor(const std::string& unit_id) const;
    std::optional<double> estimatedMlpFor(const DataUnit& u) const;

private:
    const AppConfig& cfg_;
};

}  // namespace memopt
