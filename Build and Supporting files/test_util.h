// test_util.h - Minimal test helpers (no external framework required).
//
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); CHECK_NEAR(a, b, eps); }
//   int main() { return RUN_ALL(); }
//
// Exit codes: 0 = all passed, 1 = failures, 77 = skipped (CTest treats 77
// as "Skipped" because CMakeLists sets SKIP_RETURN_CODE 77).
#pragma once

#include <cmath>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace t {

struct Case {
    const char* name;
    std::function<void()> fn;
};
inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}
inline int& failures() {
    static int f = 0;
    return f;
}
struct Registrar {
    Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline void report(bool ok, const char* expr, const char* file, int line, const std::string& extra = "") {
    if (ok) return;
    ++failures();
    std::cerr << "  FAILED: " << expr << "  (" << file << ":" << line << ")";
    if (!extra.empty()) std::cerr << "  " << extra;
    std::cerr << "\n";
}

inline int runAll() {
    int before = 0;
    for (const auto& c : registry()) {
        before = failures();
        std::cout << "[ RUN  ] " << c.name << std::endl;
        try {
            c.fn();
        } catch (const std::exception& e) {
            ++failures();
            std::cerr << "  EXCEPTION: " << e.what() << "\n";
        }
        std::cout << (failures() == before ? "[  OK  ] " : "[ FAIL ] ") << c.name << std::endl;
    }
    std::cout << registry().size() << " test case(s), " << failures() << " failed check(s)" << std::endl;
    return failures() == 0 ? 0 : 1;
}

}  // namespace t

#define TEST(name)                                         \
    static void name();                                    \
    static t::Registrar reg_##name(#name, name);           \
    static void name()

#define CHECK(cond) t::report(static_cast<bool>(cond), #cond, __FILE__, __LINE__)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        auto _va = (a);                                                             \
        auto _vb = (b);                                                             \
        std::ostringstream _os;                                                     \
        _os << "got [" << _va << "] expected [" << _vb << "]";                      \
        t::report(_va == _vb, #a " == " #b, __FILE__, __LINE__, _os.str());         \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                       \
    do {                                                                            \
        double _va = (a), _vb = (b);                                                \
        std::ostringstream _os;                                                     \
        _os << "got [" << _va << "] expected [" << _vb << "]";                      \
        t::report(std::fabs(_va - _vb) <= (eps), #a " ~= " #b, __FILE__, __LINE__, _os.str()); \
    } while (0)

#define RUN_ALL() t::runAll()

#define SKIP_TEST(reason)                                                           \
    do {                                                                            \
        std::cout << "SKIPPED: " << reason << std::endl;                            \
        return 77;                                                                  \
    } while (0)
