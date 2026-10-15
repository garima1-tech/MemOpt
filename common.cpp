#include "common.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace memopt {

std::string tierName(Tier t) {
    switch (t) {
        case Tier::FAST: return "FAST";
        case Tier::SLOW: return "SLOW";
        default: return "NONE";
    }
}

Tier parseTier(const std::string& s) {
    std::string u = toUpper(trim(s));
    if (u == "FAST") return Tier::FAST;
    if (u == "SLOW") return Tier::SLOW;
    return Tier::NONE;
}

std::string metricKindName(MetricKind k) {
    switch (k) {
        case MetricKind::Measured: return "measured";
        case MetricKind::Estimated: return "estimated";
        case MetricKind::Synthetic: return "synthetic";
        default: return "unavailable";
    }
}

Metric Metric::unavailable(const std::string& name, const std::string& unit,
                           const std::string& source, const std::string& why) {
    Metric m;
    m.name = name;
    m.unit = unit;
    m.source = source;
    m.kind = MetricKind::Unavailable;
    m.status = why;
    return m;
}

static int kindRank(MetricKind k) {
    switch (k) {
        case MetricKind::Measured: return 3;
        case MetricKind::Estimated: return 2;
        case MetricKind::Synthetic: return 1;
        default: return 0;
    }
}

MetricKind weakerKind(MetricKind a, MetricKind b) {
    return kindRank(a) <= kindRank(b) ? a : b;
}

std::string patternName(AccessPattern p) {
    switch (p) {
        case AccessPattern::Sequential: return "sequential";
        case AccessPattern::Random: return "random";
        default: return "repeated";
    }
}

static std::tm localTm() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

std::string nowIso8601() {
    std::tm tm = localTm();
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return os.str();
}

std::string timestampForFile() {
    std::tm tm = localTm();
    std::ostringstream os;
    os << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return os.str();
}

std::string formatDouble(double v, int precision) {
    if (!std::isfinite(v)) return "0";
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << v;
    return os.str();
}

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::optional<double> percentDifference(double baseline, double value) {
    if (baseline == 0.0 || !std::isfinite(baseline) || !std::isfinite(value)) return std::nullopt;
    return (value - baseline) / baseline * 100.0;
}

}  // namespace memopt
