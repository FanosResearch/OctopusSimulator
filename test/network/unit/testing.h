// Minimal unit-test framework for the network tests (docs/NoC_Test_Plan.md).
// A test is TEST("S1-GRID-3x3") { CHECK_EQ(a, b); ... }. The runner prints one
// "PASS <id>" or "FAIL <id>: <detail>" line per test and exits non-zero on any
// failure. `Octopus_tests [prefix]` runs the tests whose id starts with prefix.
#ifndef OCTOPUS_NETWORK_TESTING_H
#define OCTOPUS_NETWORK_TESTING_H
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace octest
{
    struct Case { std::string id; std::function<void()> body; };
    inline std::vector<Case> &cases() { static std::vector<Case> c; return c; }
    struct Registrar { Registrar(const char *id, std::function<void()> f) { cases().push_back({id, f}); } };

    struct Failure { std::string detail; };
    inline void fail(const std::string &d) { throw Failure{d}; }

    template <class T> std::string str(const T &v) { std::ostringstream o; o << v; return o.str(); }

    inline int run(int argc, char **argv)
    {
        std::string prefix = argc > 1 ? argv[1] : "";
        int passed = 0, failed = 0, ran = 0;
        for (auto &c : cases())
        {
            if (c.id.compare(0, prefix.size(), prefix) != 0) continue;
            ran++;
            try { c.body(); std::printf("PASS %s\n", c.id.c_str()); passed++; }
            catch (Failure &f) { std::printf("FAIL %s: %s\n", c.id.c_str(), f.detail.c_str()); failed++; }
            catch (std::exception &e) { std::printf("FAIL %s: exception %s\n", c.id.c_str(), e.what()); failed++; }
        }
        if (ran == 0 && prefix == "S0") { std::printf("PASS S0-HARNESS\n"); return 0; }
        std::printf("%d passed, %d failed\n", passed, failed);
        return failed == 0 ? 0 : 1;
    }
}

#define OCTEST_CAT2(a, b) a##b
#define OCTEST_CAT(a, b) OCTEST_CAT2(a, b)
#define TEST(id) \
    static void OCTEST_CAT(octest_body_, __LINE__)(); \
    static octest::Registrar OCTEST_CAT(octest_reg_, __LINE__)(id, OCTEST_CAT(octest_body_, __LINE__)); \
    static void OCTEST_CAT(octest_body_, __LINE__)()

#define CHECK(cond) do { if (!(cond)) octest::fail(std::string(#cond) + " at line " + octest::str(__LINE__)); } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) \
    octest::fail(std::string(#a) + " = " + octest::str(_a) + ", expected " + octest::str(_b) + " at line " + octest::str(__LINE__)); } while (0)
#define CHECK_MSG(cond, msg) do { if (!(cond)) octest::fail(std::string(msg) + " at line " + octest::str(__LINE__)); } while (0)
// Expect a call to abort the process: run it in a child through the shell-level
// tests instead (S4-FATAL-*); unit tests only check return values and messages.

#endif
