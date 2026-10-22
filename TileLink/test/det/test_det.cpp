/*
 * File  :      test_det.cpp
 * TL-DET (plan §6, §3.2 determinism): system 1 (TL-UL) and system 2 (TL-UH) of configs/ run three
 * times with the same seed through tlsim: transcript.txt and TLStats.csv byte-identical (and so
 * of identical md5). A different seed gives a different transcript.
 */
#include "TestMain.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/wait.h>

namespace
{
    std::string slurp(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss; ss << in.rdbuf();
        return ss.str();
    }
    // FNV-1a, a digest printed with the sizes (the comparison itself is byte for byte).
    unsigned long long fnv(const std::string &s)
    {
        unsigned long long h = 1469598103934665603ull;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
        return h;
    }
    int tlsim(const std::string &sys, const std::string &dir, int seed)
    {
        std::string cmd = std::string("\"") + TLSIM_PATH + "\" -c " + TL_CONFIG_DIR + "/" + sys + ".csv -o " + dir +
                          " --trace -p \"seed(i)=" + std::to_string(seed) + "\" > " + dir + ".log 2>&1";
        int st = std::system(cmd.c_str());
        return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    }
    void checkSystem(const std::string &sys)
    {
        std::string tr[3], stats[3];
        for (int r = 0; r < 3; r++)
        {
            std::string dir = "det_" + sys + "_" + std::to_string(r);
            CHECK_EQ(tlsim(sys, dir, 1), 0);
            tr[r] = slurp(dir + "/transcript.txt");
            stats[r] = slurp(dir + "/TLStats.csv");
        }
        CHECK(tr[0].size() > 1000);
        CHECK(stats[0].size() > 100);
        for (int r = 1; r < 3; r++) { CHECK(tr[r] == tr[0]); CHECK(stats[r] == stats[0]); }
        std::printf("  %s seed 1 x3: transcript %zu bytes fnv %016llx, TLStats %zu bytes fnv %016llx\n", sys.c_str(),
                    tr[0].size(), fnv(tr[0]), stats[0].size(), fnv(stats[0]));
        std::string dir = "det_" + sys + "_seed2";
        CHECK_EQ(tlsim(sys, dir, 2), 0);
        std::string other = slurp(dir + "/transcript.txt");
        CHECK(other.size() > 1000);
        CHECK(other != tr[0]);
        std::printf("  %s seed 2: transcript %zu bytes fnv %016llx (differs)\n", sys.c_str(), other.size(), fnv(other));
    }
}

TEST(system1_ul_deterministic) { checkSystem("system1_ul"); }
TEST(system2_uh_deterministic) { checkSystem("system2_uh"); }

TL_TEST_MAIN("TL-DET")
