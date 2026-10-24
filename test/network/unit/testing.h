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
#include <sys/wait.h>
#include <unistd.h>

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

// Child cases (a case that may end the process, or must be shown to): CHILD("name") { ... } runs only in a child
// process, `Octopus_tests --child name`; it prints "CHILD-DONE <name>" when its body returns. runChild runs it and
// collects its status and its output (stdout and stderr). A positive case requires status 0 AND the marker
// (CHECK_CHILD_OK): code under test that calls exit(0) would pass a status-only check.
namespace octest
{
    struct Child { std::string name; std::function<void()> body; };
    inline std::vector<Child> &children() { static std::vector<Child> c; return c; }
    struct ChildRegistrar { ChildRegistrar(const char *n, std::function<void()> f) { children().push_back({n, f}); } };
    inline int childMain(const std::string &name)
    {
        for (auto &c : children())
            if (c.name == name)
            {
                try { c.body(); }
                catch (Failure &f) { std::printf("CHILD-FAIL %s: %s\n", name.c_str(), f.detail.c_str()); std::fflush(stdout); return 1; }
                std::printf("CHILD-DONE %s\n", name.c_str()); std::fflush(stdout);
                return 0;
            }
        std::printf("CHILD-UNKNOWN %s\n", name.c_str());
        return 3;
    }
    struct ChildResult
    {
        int status = -1; std::string out; std::string name;
        bool done() const { return out.find("CHILD-DONE " + name + "\n") != std::string::npos; }
    };
    inline ChildResult runChild(const std::string &name)
    {
        ChildResult r; r.name = name;
        int p[2];
        if (pipe(p) != 0) return r;
        std::fflush(stdout);
        pid_t pid = fork();
        if (pid == 0)
        {
            dup2(p[1], 1); dup2(p[1], 2); close(p[0]); close(p[1]);
            execl("/proc/self/exe", "Octopus_tests", "--child", name.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        close(p[1]);
        char buf[4096]; ssize_t n;
        while ((n = read(p[0], buf, sizeof buf)) > 0) r.out.append(buf, size_t(n));
        close(p[0]);
        int st = 0;
        if (waitpid(pid, &st, 0) == pid) r.status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
        return r;
    }
}

#define OCTEST_CAT2(a, b) a##b
#define OCTEST_CAT(a, b) OCTEST_CAT2(a, b)
#define TEST(id) \
    static void OCTEST_CAT(octest_body_, __LINE__)(); \
    static octest::Registrar OCTEST_CAT(octest_reg_, __LINE__)(id, OCTEST_CAT(octest_body_, __LINE__)); \
    static void OCTEST_CAT(octest_body_, __LINE__)()

#define CHILD(name) \
    static void OCTEST_CAT(octest_child_, __LINE__)(); \
    static octest::ChildRegistrar OCTEST_CAT(octest_creg_, __LINE__)(name, OCTEST_CAT(octest_child_, __LINE__)); \
    static void OCTEST_CAT(octest_child_, __LINE__)()
#define CHECK_CHILD_OK(name) do { octest::ChildResult _r = octest::runChild(name); \
    if (!(_r.status == 0 && _r.done())) octest::fail("child " + std::string(name) + ": status " + octest::str(_r.status) + \
        (_r.done() ? "" : ", no completion marker") + "; output: " + _r.out.substr(_r.out.size() > 600 ? _r.out.size() - 600 : 0)); } while (0)
#define CHECK(cond) do { if (!(cond)) octest::fail(std::string(#cond) + " at line " + octest::str(__LINE__)); } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) \
    octest::fail(std::string(#a) + " = " + octest::str(_a) + ", expected " + octest::str(_b) + " at line " + octest::str(__LINE__)); } while (0)
#define CHECK_MSG(cond, msg) do { if (!(cond)) octest::fail(std::string(msg) + " at line " + octest::str(__LINE__)); } while (0)
// Expect a call to abort the process: run it in a child through the shell-level
// tests instead (S4-FATAL-*); unit tests only check return values and messages.

#endif
