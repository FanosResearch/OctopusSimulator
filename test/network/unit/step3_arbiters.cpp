// Step 3 tests: Candidate / electIndex on the arbiters (docs/NoC_Test_Plan.md §6).
#include "testing.h"
#include "Arbiter.h"
#include "FCFSArbiter.h"
#include "RRArbiter.h"
#include <algorithm>
#include <random>
using namespace octopus;

// deterministic PRNG so the tests are reproducible
static std::mt19937_64 rng(12345);
static std::vector<int> OWNERS = {0, 1, 2, 3};

TEST("S3-FCFS")
{
    FCFSArbiter a(&OWNERS, 1);
    for (int round = 0; round < 1000; round++)
    {
        int n = 1 + rng() % 8; std::vector<Candidate> c;
        for (int i = 0; i < n; i++) c.push_back({i, (int)(rng() % 4), rng() % 1000});
        int w = a.electIndex(round, c);
        uint64_t minseq = ~0ull; for (auto &k : c) minseq = std::min(minseq, k.seq);
        CHECK_EQ(c[w].seq, minseq);
        // ties by lowest index
        for (auto &k : c) if (k.seq == minseq) CHECK(w <= k.index);
    }
    CHECK_EQ(a.electIndex(0, {}), -1);
}
TEST("S3-RR")
{
    RRArbiter a(&OWNERS, 1);
    // every owner with a candidate is served once per round; an owner without one does not
    // consume the rotation: with owners {0,1,2,3} and candidates only from {1,3}, the winners
    // alternate 1,3,1,3 ...
    std::vector<Candidate> c = {{0, 1, 10}, {1, 3, 11}, {2, 1, 12}, {3, 3, 13}};
    std::vector<int> owners_won;
    for (int i = 0; i < 6; i++) { int w = a.electIndex(i, c); owners_won.push_back(c[w].owner); }
    std::vector<int> want = {1, 3, 1, 3, 1, 3};
    CHECK(owners_won == want);
    // within an owner, its lowest seq: owner 1 -> index 0 (seq 10), not index 2 (seq 12)
    RRArbiter b(&OWNERS, 1);
    CHECK_EQ(b.electIndex(0, c), 0);
    CHECK_EQ(b.electIndex(1, c), 1);
    // full round with all four present: each served exactly once per four elections, 100 rounds
    RRArbiter d(&OWNERS, 1); std::vector<Candidate> all = {{0, 0, 1}, {1, 1, 2}, {2, 2, 3}, {3, 3, 4}};
    std::vector<int> count(4, 0);
    for (int i = 0; i < 400; i++) count[all[d.electIndex(i, all)].owner]++;
    for (int o = 0; o < 4; o++) CHECK_EQ(count[o], 100);
    CHECK_EQ(d.electIndex(0, {}), -1);
}
// equivalence: the same draw as message buffers to elect() and as candidates to electIndex()
static void draw(std::vector<std::vector<Message>> &bufs, std::vector<Candidate> &cands)
{
    int n = 1 + rng() % 6; bufs.assign(n, {}); cands.clear();
    std::vector<uint64_t> seqs; for (int i = 0; i < n; i++) seqs.push_back(i);
    std::shuffle(seqs.begin(), seqs.end(), rng);
    for (int i = 0; i < n; i++)
    {
        Message m; m.owner = rng() % 4; m.cycle = seqs[i]; m.msg_id = 1000 + i;
        bufs[i].push_back(m); cands.push_back({i, m.owner, seqs[i]});
    }
}
TEST("S3-EQUIV")
{
    for (int policy = 0; policy < 2; policy++)
    {
        Arbiter *ea = policy ? (Arbiter *)new RRArbiter(&OWNERS, 1) : new FCFSArbiter(&OWNERS, 1);
        Arbiter *ia = policy ? (Arbiter *)new RRArbiter(&OWNERS, 1) : new FCFSArbiter(&OWNERS, 1);
        for (int round = 0; round < 1000; round++)
        {
            std::vector<std::vector<Message>> bufs; std::vector<Candidate> cands; draw(bufs, cands);
            std::vector<std::vector<Message> *> ptrs; for (auto &b : bufs) ptrs.push_back(&b);
            Message out; bool ok = ea->elect(round, ptrs, &out);
            int w = ia->electIndex(round, cands);
            CHECK_EQ(ok, w >= 0);
            if (ok) CHECK_MSG((int)out.msg_id == 1000 + w, "policy " + octest::str(policy) + " round " + octest::str(round) + ": elect chose " + octest::str(out.msg_id - 1000) + ", electIndex " + octest::str(w));
        }
        delete ea; delete ia;
    }
}
TEST("S3-STATE")
{
    // one RR arbiter driven alternately through elect() and electIndex(): one rotation
    RRArbiter a(&OWNERS, 1);
    std::vector<int> seen;
    for (int i = 0; i < 8; i++)
    {
        if (i % 2 == 0)
        {
            std::vector<std::vector<Message>> bufs(4); for (int o = 0; o < 4; o++) { Message m; m.owner = o; m.cycle = o; bufs[o].push_back(m); }
            std::vector<std::vector<Message> *> ptrs; for (auto &b : bufs) ptrs.push_back(&b);
            Message out; CHECK(a.elect(i, ptrs, &out)); seen.push_back(out.owner);
        }
        else
        {
            std::vector<Candidate> c = {{0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {3, 3, 3}};
            seen.push_back(c[a.electIndex(i, c)].owner);
        }
    }
    std::vector<int> want = {1, 2, 3, 0, 1, 2, 3, 0};
    CHECK(seen == want);
}
