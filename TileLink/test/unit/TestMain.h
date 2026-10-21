/*
 * File  :      TestMain.h
 * A tiny self-contained test framework for TL-UNIT (plan §6): TEST(name) registers a case,
 * CHECK/CHECK_EQ record failures, TL_TEST_MAIN runs every case and returns 1 on any failure.
 */
#ifndef _TL_TEST_MAIN_H
#define _TL_TEST_MAIN_H
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace tltest
{
    struct Case { const char *name; std::function<void()> body; };
    inline std::vector<Case> &cases() { static std::vector<Case> v; return v; }
    inline int &failures() { static int n = 0; return n; }
    inline int &checks() { static int n = 0; return n; }
    struct Register { Register(const char *n, std::function<void()> b) { cases().push_back({n, b}); } };

    template <typename T> std::string str(const T &v) { std::ostringstream o; o << v; return o.str(); }
    inline std::string str(unsigned char v) { return std::to_string(unsigned(v)); }

    inline void fail(const char *file, int line, const std::string &what)
    {
        failures()++;
        std::printf("  FAIL %s:%d: %s\n", file, line, what.c_str());
    }
}

#define TEST(name) \
    static void tl_test_##name(); \
    static tltest::Register tl_reg_##name(#name, tl_test_##name); \
    static void tl_test_##name()

#define CHECK(cond) do { tltest::checks()++; if (!(cond)) tltest::fail(__FILE__, __LINE__, #cond); } while (0)
#define CHECK_EQ(a, b) do { tltest::checks()++; auto _a = (a); auto _b = (b); if (!(_a == _b)) \
    tltest::fail(__FILE__, __LINE__, std::string(#a " == " #b " (") + tltest::str(_a) + " vs " + tltest::str(_b) + ")"); } while (0)

#define TL_TEST_MAIN(suite) \
    int main() \
    { \
        int failed_cases = 0; \
        for (auto &c : tltest::cases()) \
        { \
            int before = tltest::failures(); \
            std::printf("[%s] %s\n", suite, c.name); \
            c.body(); \
            if (tltest::failures() != before) failed_cases++; \
        } \
        std::printf("%s: %zu cases, %d checks, %d failed\n", suite, tltest::cases().size(), tltest::checks(), failed_cases); \
        return failed_cases ? 1 : 0; \
    }
#endif
