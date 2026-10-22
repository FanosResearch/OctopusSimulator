/*
 * File  :      Subprocess.h
 * Child-process cases for the TL-GRAPH and TL-PROGRESS tests. A refused configuration and a
 * watchdog trip in fatal mode end in tl::fatal (exit 2), so the cases that must fail run in a
 * child: the test binary re-executes itself with "--case <name>", the child runs that case
 * (registered with CHILD_CASE) and the parent checks the exit status and the child's stderr.
 * TL_TEST_MAIN_WITH_CHILDREN replaces TestMain.h's TL_TEST_MAIN: without arguments it runs
 * the suite, with "--case <name>" it runs that child case and exits 0 if the case returns.
 */
#ifndef _TL_TEST_SUBPROCESS_H
#define _TL_TEST_SUBPROCESS_H
#include "TestMain.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace tltest
{
    struct ChildCase { const char *name; void (*body)(); };
    inline std::vector<ChildCase> &childCases() { static std::vector<ChildCase> v; return v; }
    struct ChildRegister { ChildRegister(const char *n, void (*b)()) { childCases().push_back({n, b}); } };
    inline std::string &selfPath() { static std::string p; return p; }

    struct ChildResult
    {
        int         status = -1;   // exit status, or -1 when the child was killed or could not run
        std::string err;           // its stderr
        std::string out;           // its stdout
        // The first line of stderr (what tl::fatal printed).
        std::string firstErrLine() const { size_t n = err.find('\n'); return n == std::string::npos ? err : err.substr(0, n); }
    };

    inline std::string readAll(int fd)
    {
        std::string s;
        char buf[4096];
        for (;;)
        {
            ssize_t n = read(fd, buf, sizeof buf);
            if (n <= 0) break;
            s.append(buf, size_t(n));
        }
        return s;
    }

    // Runs the child case `name` in a subprocess and collects its exit status and output.
    inline ChildResult runCase(const std::string &name)
    {
        ChildResult r;
        int errp[2], outp[2];
        if (pipe(errp) != 0 || pipe(outp) != 0) return r;
        pid_t pid = fork();
        if (pid < 0) return r;
        if (pid == 0)
        {
            dup2(errp[1], 2);
            dup2(outp[1], 1);
            close(errp[0]); close(errp[1]); close(outp[0]); close(outp[1]);
            const char *exe = access("/proc/self/exe", X_OK) == 0 ? "/proc/self/exe" : selfPath().c_str();
            execl(exe, selfPath().c_str(), "--case", name.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }
        close(errp[1]); close(outp[1]);
        // stderr first: the dumps are small (well under the pipe buffer), so no interleaving is needed
        r.err = readAll(errp[0]);
        r.out = readAll(outp[0]);
        close(errp[0]); close(outp[0]);
        int st = 0;
        if (waitpid(pid, &st, 0) == pid && WIFEXITED(st)) r.status = WEXITSTATUS(st);
        return r;
    }
}

#define CHILD_CASE(name) \
    static void tl_child_##name(); \
    static tltest::ChildRegister tl_creg_##name(#name, tl_child_##name); \
    static void tl_child_##name()

#define TL_TEST_MAIN_WITH_CHILDREN(suite) \
    int main(int argc, char *argv[]) \
    { \
        tltest::selfPath() = argv[0]; \
        if (argc == 3 && std::string(argv[1]) == "--case") \
        { \
            for (auto &c : tltest::childCases()) if (std::string(argv[2]) == c.name) { c.body(); return 0; } \
            std::fprintf(stderr, "unknown child case %s\n", argv[2]); \
            return 3; \
        } \
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
