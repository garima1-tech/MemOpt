#include "profiler.h"

#include <cerrno>
#include <cstring>

#ifdef __linux__
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace memopt {

// ------------------------------------------------------ FallbackProfiler ---
bool FallbackProfiler::available(std::string& why) const {
    why = "always available; collects no hardware counters";
    return true;
}

bool FallbackProfiler::start(int) { return true; }

ProfileReport FallbackProfiler::stop() {
    ProfileReport r;
    r.profiler = name();
    r.scope = "none";
    r.hardware_counters = false;
    const char* why = "not collected: fallback profiler (no hardware counter access on this platform/config)";
    r.counters.push_back(Metric::unavailable("cycles", "count", "fallback", why));
    r.counters.push_back(Metric::unavailable("instructions", "count", "fallback", why));
    r.counters.push_back(Metric::unavailable("cache_misses", "count", "fallback", why));
    r.counters.push_back(Metric::unavailable("memory_latency", "ns", "fallback",
                                             "not measurable without PEBS/IBS-style sampling"));
    r.counters.push_back(Metric::unavailable("mlp", "ratio", "fallback",
                                             "not measurable without PEBS/IBS-style sampling"));
    r.status = "Fallback profiler: no counters collected. Query timings are still real.";
    return r;
}

// ----------------------------------------------------- LinuxPerfProfiler ---
struct LinuxPerfProfiler::Counter {
    std::string name;
    std::string unit;
    bool hardware = true;
    int fd = -1;
    std::string error;
};

LinuxPerfProfiler::LinuxPerfProfiler() = default;
LinuxPerfProfiler::~LinuxPerfProfiler() { closeAll(); }

void LinuxPerfProfiler::closeAll() {
    for (auto* c : counters_) {
#ifdef __linux__
        if (c->fd >= 0) close(c->fd);
#endif
        delete c;
    }
    counters_.clear();
}

#ifdef __linux__
static int perfOpen(perf_event_attr* attr, int pid) {
    return static_cast<int>(syscall(SYS_perf_event_open, attr, pid, -1, -1, 0));
}

struct EventSpec {
    const char* name;
    const char* unit;
    unsigned type;
    unsigned long long config;
    bool hardware;
};

static const EventSpec kEvents[] = {
    {"cycles", "count", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, true},
    {"instructions", "count", PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, true},
    {"cache_references", "count", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES, true},
    {"cache_misses", "count", PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES, true},
    {"llc_load_misses", "count", PERF_TYPE_HW_CACHE,
     PERF_COUNT_HW_CACHE_LL | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16), true},
    {"task_clock", "ns", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK, false},
    {"page_faults", "count", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS, false},
    {"context_switches", "count", PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES, false},
};
#endif

bool LinuxPerfProfiler::available(std::string& why) const {
#ifdef __linux__
    perf_event_attr a{};
    a.size = sizeof a;
    a.type = PERF_TYPE_SOFTWARE;
    a.config = PERF_COUNT_SW_TASK_CLOCK;
    a.disabled = 1;
    a.exclude_kernel = 1;
    a.exclude_hv = 1;
    int fd = perfOpen(&a, 0);
    if (fd < 0) {
        why = std::string("perf_event_open failed: ") + std::strerror(errno) +
              ". Try: sudo sysctl kernel.perf_event_paranoid=1 (Linux) or run under WSL2.";
        return false;
    }
    close(fd);
    why = "perf_event_open works (individual hardware events may still be unsupported)";
    return true;
#else
    why = "perf_event_open is Linux-only. On Windows use WSL2 for hardware counters.";
    return false;
#endif
}

bool LinuxPerfProfiler::start(int target_pid) {
    closeAll();
    start_note_.clear();
#ifdef __linux__
    auto openFor = [&](int pid) -> int {
        int opened = 0;
        for (const auto& e : kEvents) {
            auto* c = new Counter{e.name, e.unit, e.hardware, -1, ""};
            perf_event_attr a{};
            a.size = sizeof a;
            a.type = e.type;
            a.config = e.config;
            a.disabled = 1;
            a.exclude_kernel = 1;
            a.exclude_hv = 1;
            a.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
            c->fd = perfOpen(&a, pid);
            if (c->fd < 0) c->error = std::strerror(errno);
            else ++opened;
            counters_.push_back(c);
        }
        return opened;
    };

    int opened = 0;
    if (target_pid > 0) {
        opened = openFor(target_pid);
        if (opened > 0) {
            scope_ = "PostgreSQL backend process (pid " + std::to_string(target_pid) + ")";
        } else {
            std::string err = counters_.empty() ? "" : counters_.front()->error;
            start_note_ = "Could not attach to PostgreSQL backend pid " + std::to_string(target_pid) +
                          " (" + err + "); counting the MemOpt client process instead. "
                          "Run MemOpt as the postgres OS user or lower kernel.perf_event_paranoid "
                          "to profile the server process.";
            closeAll();
        }
    }
    if (opened == 0) {
        opened = openFor(0);
        scope_ = "MemOpt client process (libpq side, not the PostgreSQL server)";
    }
    if (opened == 0) {
        start_note_ += " No perf counters could be opened.";
        return false;
    }
    for (auto* c : counters_) {
        if (c->fd >= 0) {
            ioctl(c->fd, PERF_EVENT_IOC_RESET, 0);
            ioctl(c->fd, PERF_EVENT_IOC_ENABLE, 0);
        }
    }
    return true;
#else
    (void)target_pid;
    start_note_ = "LinuxPerfProfiler is not supported on this platform.";
    return false;
#endif
}

ProfileReport LinuxPerfProfiler::stop() {
    ProfileReport r;
    r.profiler = name();
    r.scope = scope_.empty() ? "none" : scope_;
#ifdef __linux__
    double cycles = -1, instructions = -1, refs = -1, misses = -1;
    int hw = 0;
    for (auto* c : counters_) {
        Metric m;
        m.name = c->name;
        m.unit = c->unit;
        m.source = "perf_event_open";
        if (c->fd < 0) {
            m.kind = MetricKind::Unavailable;
            m.status = (c->hardware ? "hardware event not available: " : "software event not available: ") + c->error;
            r.counters.push_back(m);
            continue;
        }
        ioctl(c->fd, PERF_EVENT_IOC_DISABLE, 0);
        unsigned long long buf[3] = {0, 0, 0};  // value, time_enabled, time_running
        ssize_t n = read(c->fd, buf, sizeof buf);
        if (n != static_cast<ssize_t>(sizeof buf)) {
            m.kind = MetricKind::Unavailable;
            m.status = "read() of counter failed";
        } else if (buf[2] == 0) {
            m.kind = MetricKind::Unavailable;
            m.status = "counter never scheduled (time_running = 0)";
        } else {
            double v = static_cast<double>(buf[0]);
            if (buf[2] < buf[1]) {
                v = v * static_cast<double>(buf[1]) / static_cast<double>(buf[2]);
                m.status = "measured (scaled: multiplexed " +
                           formatDouble(100.0 * buf[2] / buf[1], 1) + "% of the time)";
            } else {
                m.status = "measured";
            }
            m.value = v;
            m.kind = MetricKind::Measured;
            if (c->hardware) ++hw;
            if (c->name == "cycles") cycles = v;
            if (c->name == "instructions") instructions = v;
            if (c->name == "cache_references") refs = v;
            if (c->name == "cache_misses") misses = v;
        }
        r.counters.push_back(m);
    }
    if (cycles > 0 && instructions >= 0) {
        Metric ipc{"ipc", instructions / cycles, "instr/cycle", "derived from perf counters",
                   MetricKind::Measured, "derived: instructions / cycles"};
        r.counters.push_back(ipc);
    }
    if (refs > 0 && misses >= 0) {
        Metric mr{"cache_miss_rate", 100.0 * misses / refs, "%", "derived from perf counters",
                  MetricKind::Measured, "derived: cache_misses / cache_references"};
        r.counters.push_back(mr);
    }
    r.hardware_counters = hw > 0;
    closeAll();
#endif
    r.counters.push_back(Metric::unavailable(
        "memory_latency", "ns", "linux-perf",
        "per-access latency needs PEBS/IBS load-latency sampling; not implemented in this prototype"));
    r.counters.push_back(Metric::unavailable("mlp", "ratio", "linux-perf",
                                             "needs offcore outstanding-request events; not implemented"));
    r.status = (r.hardware_counters ? "Hardware counters measured. " : "No hardware counters available (VM/container or unsupported CPU); software counters only. ") +
               start_note_;
    return r;
}

// -------------------------------------------------------------- factory ---
std::unique_ptr<IProfiler> createProfiler(const AppConfig& cfg, std::string& note) {
    std::string want = toLower(cfg.profiler);
    if (want == "fallback") {
        note = "Fallback profiler selected by configuration (MEMOPT_PROFILER=fallback).";
        return std::make_unique<FallbackProfiler>();
    }
    auto perf = std::make_unique<LinuxPerfProfiler>();
    std::string why;
    if (perf->available(why)) {
        note = "Linux perf profiler: " + why;
        return perf;
    }
    note = "Linux perf profiler unavailable (" + why + "). Using fallback profiler.";
    return std::make_unique<FallbackProfiler>();
}

}  // namespace memopt
