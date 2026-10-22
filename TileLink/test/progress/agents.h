/*
 * File  :      agents.h
 * The agents of the §5.2 conformance examples, as small test-local Agents (TL-PROGRESS,
 * docs/TileLink_Negative_Tests.md §6): a scripted master, the periodic forwarder (example 1,
 * non-conforming), a periodically ready slave (example 2's legal counterpart), the master
 * withholding beats of a received burst (example 3, non-conforming), the very slow arbiter
 * (example 4, conforming), a refresh-waiting RAM (example 2) and a tiny fuzz master. All
 * TL-UH, A and D only, data mode none. None of them is a T2/T3 agent.
 */
#ifndef _TL_TEST_PROGRESS_AGENTS_H
#define _TL_TEST_PROGRESS_AGENTS_H
#include "tilelink/Kernel.h"
#include <deque>
#include <utility>
#include <vector>

namespace tlp
{
    using namespace octopus::tl;

    // A new A request laid out for link l, with the simulator fields set.
    inline MsgPtr newRequest(Agent &from, Link &l, OpA op, unsigned size, uint64_t addr, uint64_t src)
    {
        MsgPtr m = Msg::make(Channel::A, unsigned(op), size, l.params().w);
        m->address = addr; m->source = src;
        m->id = from.kernel()->newMsgId(); m->from_agent = from.id();
        m->deriveMask();
        return m;
    }
    // Table 12: AccessAckData answers a Get, AccessAck a Put; size and source copied (§6.4).
    inline MsgPtr responseTo(Agent &from, Link &l, const Msg &req)
    {
        unsigned op = req.opcode == unsigned(OpA::Get) ? unsigned(OpD::AccessAckData) : unsigned(OpD::AccessAck);
        MsgPtr r = Msg::make(Channel::D, op, req.size, l.params().w);
        r->source = req.source;
        r->id = from.kernel()->newMsgId(); r->from_agent = from.id();
        r->deriveMask();
        return r;
    }

    // One message being sent beat by beat on one channel: offer() presents the next beat,
    // commit() advances on acceptance. valid is held until the beat is accepted.
    struct Tx
    {
        MsgPtr   msg;
        unsigned beat = 0;
        void start(const MsgPtr &m) { msg = m; beat = 0; }
        void offer(ChannelState &c) { if (msg) c.offer(msg, beat); }
        void commit(ChannelState &c)
        {
            const Beat &b = c.accepted();
            if (!b.msg || b.msg != msg) return;
            if (b.last()) msg = nullptr; else beat = b.index + 1;
        }
    };

    // Offers a script of (cycle, message) on its one master link, in order, holding valid;
    // always ready on D; records the cycle each response completed.
    class ScriptedMaster : public Agent
    {
    public:
        explicit ScriptedMaster(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::UH; }
        std::vector<std::pair<uint64_t, MsgPtr>> script;
        std::vector<std::pair<uint64_t, MsgPtr>> answered;
        Link &L() const { return *m_links[0].link; }
        void offer(uint64_t t) override
        {
            if (!tx.msg && cur < script.size() && t >= script[cur].first) tx.start(script[cur++].second);
            tx.offer(L().chan(Channel::A));
        }
        void accept(uint64_t) override { L().chan(Channel::D).setReady(true); }
        void commit(uint64_t t) override
        {
            tx.commit(L().chan(Channel::A));
            const Beat &b = L().chan(Channel::D).accepted();
            if (b.msg && b.last()) answered.push_back({t, b.msg});
        }
        bool idle() const override { return !tx.msg && cur >= script.size() && answered.size() >= script.size(); }
    private:
        Tx tx; size_t cur = 0;
    };

    // §5.2 example 1: a two-link agent forwarding A beats from its slave link to its master
    // link and D beats back, one beat buffered per direction (plan §3.2: one cycle in, one
    // out). `periodic` is the mutant: it raises valid on the outgoing A only on even cycles,
    // so the forwarded beat is never presented (§5.1) and the received request never answered.
    class Forwarder : public Agent
    {
    public:
        explicit Forwarder(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::UH; }
        bool periodic = false;
        Link &in() const { for (auto &l : m_links) if (l.role == Role::Slave) return *l.link; return *m_links[0].link; }
        Link &out() const { for (auto &l : m_links) if (l.role == Role::Master) return *l.link; return *m_links[0].link; }
        void offer(uint64_t t) override
        {
            if (a.msg && (!periodic || t % 2 == 0)) out().chan(Channel::A).offer(a.msg, a.index);
            if (d.msg) in().chan(Channel::D).offer(d.msg, d.index);
        }
        void accept(uint64_t) override
        {
            in().chan(Channel::A).setReady(!a.msg);
            out().chan(Channel::D).setReady(!d.msg);
        }
        void commit(uint64_t) override
        {
            if (out().chan(Channel::A).accepted().msg) a = Beat();
            if (in().chan(Channel::D).accepted().msg) d = Beat();
            if (in().chan(Channel::A).accepted().msg) a = in().chan(Channel::A).accepted();
            if (out().chan(Channel::D).accepted().msg) d = out().chan(Channel::D).accepted();
        }
        bool idle() const override { return !a.msg && !d.msg; }
    private:
        Beat a, d;
    };

    // §5.2 example 2's legal shape: a_ready HIGH only on odd cycles (periodic readiness);
    // answers each request in the cycle after its last beat was accepted.
    class PeriodicReadySlave : public Agent
    {
    public:
        explicit PeriodicReadySlave(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::UH; }
        Link &L() const { return *m_links[0].link; }
        void offer(uint64_t) override
        {
            if (!tx.msg && !resp.empty()) { tx.start(resp.front()); resp.pop_front(); }
            tx.offer(L().chan(Channel::D));
        }
        void accept(uint64_t t) override { L().chan(Channel::A).setReady(t % 2 == 1); }
        void commit(uint64_t) override
        {
            tx.commit(L().chan(Channel::D));
            const Beat &b = L().chan(Channel::A).accepted();
            if (b.msg && b.last()) resp.push_back(responseTo(*this, L(), *b.msg));
        }
        bool idle() const override { return !tx.msg && resp.empty(); }
    private:
        Tx tx; std::deque<MsgPtr> resp;
    };

    // §5.2 example 3: two master links. Burst A (PutFullData size 5, 4 beats on w=8) starts
    // on link 1 at t0; burst B on link 2 at t0+1. The mutant (conforming=false) lowers valid
    // on link 1 after A's first beat was accepted and waits for B's first beat to be accepted
    // before resuming A: rule 2 broken on link 1. The conforming agent finishes A, then sends B.
    class WithholdingMaster : public Agent
    {
    public:
        explicit WithholdingMaster(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::UH; }
        bool conforming = false;
        uint64_t t0 = 100;
        unsigned answered = 0;
        Link &l1() const { return *m_links[0].link; }
        Link &l2() const { return *m_links[1].link; }
        void offer(uint64_t t) override
        {
            if (!mA)
            {
                mA = newRequest(*this, l1(), OpA::PutFullData, 5, 0x100, 0);
                mB = newRequest(*this, l2(), OpA::PutFullData, 5, 0x200, 0);
                A.start(mA); B.start(mB);
            }
            bool a_active = t >= t0 && A.msg;
            bool b_active = t >= t0 + 1 && B.msg && (!conforming || !A.msg);
            bool a_received = A.beat > 0;               // A's first beat accepted: in progress (§5.1)
            bool b_received = !B.msg || B.beat > 0;
            if (a_active && (conforming || !a_received || b_received)) A.offer(l1().chan(Channel::A));
            if (b_active) B.offer(l2().chan(Channel::A));
        }
        void accept(uint64_t) override
        {
            l1().chan(Channel::D).setReady(true);
            l2().chan(Channel::D).setReady(true);
        }
        void commit(uint64_t) override
        {
            A.commit(l1().chan(Channel::A));
            B.commit(l2().chan(Channel::A));
            for (Link *l : {&l1(), &l2()})
            {
                const Beat &b = l->chan(Channel::D).accepted();
                if (b.msg && b.last()) answered++;
            }
        }
        bool idle() const override { return mA && !A.msg && !B.msg && answered >= 2; }
    private:
        MsgPtr mA, mB; Tx A, B;
    };

    // §5.2 example 4: arbitrates the A channels of two incoming links onto one outgoing link,
    // one transaction at a time. Idle: selects a valid request (ties to the first link) and
    // lowers ready on the other. Busy: forwards the request's beats, then lowers ready on both
    // incoming A channels until the response's last beat has been accepted by the selected
    // link. One beat buffered per direction (no pass-through, plan §3.2). Conforming: it can
    // assume rule 2 of its incoming links, and rule 3 exempts it while its outgoing link has a
    // message in flight or unanswered.
    class SlowArbiter : public Agent
    {
    public:
        explicit SlowArbiter(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::UH; }
        ProgressHints hints() const override { ProgressHints h; h.queue_depth = 1; return h; }
        void reset(uint64_t) override
        {
            for (auto &l : m_links) (l.role == Role::Slave ? in : outv).push_back(l.link);
        }
        void offer(uint64_t) override
        {
            if (a.msg) outv[0]->chan(Channel::A).offer(a.msg, a.index);
            if (d.msg && sel >= 0) in[size_t(sel)]->chan(Channel::D).offer(d.msg, d.index);
        }
        void accept(uint64_t) override
        {
            // §5.2: "select a valid request from either incoming A channel"; on a tie, the link
            // after the one served last (round robin), the first link at the start
            if (sel < 0)
                for (size_t k = 0; k < in.size() && sel < 0; k++)
                {
                    size_t i = (size_t(last + 1) + k) % in.size();
                    if (in[i]->chan(Channel::A).valid()) { sel = int(i); last = sel; }
                }
            for (size_t i = 0; i < in.size(); i++) in[i]->chan(Channel::A).setReady(sel == int(i) && !req_done && !a.msg);
            outv[0]->chan(Channel::D).setReady(!d.msg);
        }
        void commit(uint64_t) override
        {
            const Beat &oa = outv[0]->chan(Channel::A).accepted();
            if (oa.msg) { if (oa.last()) req_done = true; a = Beat(); }
            if (sel >= 0)
            {
                const Beat &sd = in[size_t(sel)]->chan(Channel::D).accepted();
                if (sd.msg) { bool last = sd.last(); d = Beat(); if (last) { sel = -1; req_done = false; } }
            }
            if (sel >= 0)
            {
                const Beat &ia = in[size_t(sel)]->chan(Channel::A).accepted();
                if (ia.msg) a = ia;
            }
            const Beat &od = outv[0]->chan(Channel::D).accepted();
            if (od.msg) d = od;
        }
        bool idle() const override { return sel < 0 && !a.msg && !d.msg; }
        int selected() const { return sel; }
    private:
        std::vector<Link *> in, outv;
        int sel = -1, last = -1; bool req_done = false;
        Beat a, d;
    };

    // §5.2 example 2: a RAM with a service latency and a periodic refresh during which it
    // lowers a_ready and starts no response (every refresh_period cycles, for refresh_cycles).
    // Beats of a response already in progress keep flowing (rule 2). Responses in request order.
    class RamSlaveLite : public Agent
    {
    public:
        explicit RamSlaveLite(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::UH; }
        uint64_t latency = 1, refresh_period = 0, refresh_cycles = 0;
        ProgressHints hints() const override { ProgressHints h; h.latency = latency; h.queue_depth = refresh_cycles > 1 ? refresh_cycles : 1; return h; }
        Link &L() const { return *m_links[0].link; }
        bool refreshing(uint64_t t) const { return refresh_period > 0 && (t % refresh_period) < refresh_cycles; }
        void offer(uint64_t t) override
        {
            if (!tx.msg && !q.empty() && q.front().second <= t && !refreshing(t))
            {
                tx.start(responseTo(*this, L(), *q.front().first));
                q.pop_front();
            }
            tx.offer(L().chan(Channel::D));
        }
        void accept(uint64_t t) override { L().chan(Channel::A).setReady(!refreshing(t)); }
        void commit(uint64_t t) override
        {
            tx.commit(L().chan(Channel::D));
            const Beat &b = L().chan(Channel::A).accepted();
            if (b.msg && b.last()) q.push_back({b.msg, t + latency});
        }
        bool idle() const override { return !tx.msg && q.empty(); }
    private:
        Tx tx; std::deque<std::pair<MsgPtr, uint64_t>> q;
    };

    // A small fuzz master: n requests (Get or PutFullData, sizes min_size..max_size, random
    // aligned addresses), at most max_outstanding unanswered, sources from a pool of that
    // size, the first request at t0 with first_size if set. Always ready on D.
    class FuzzLite : public Agent
    {
    public:
        explicit FuzzLite(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::UH; }
        unsigned n = 4, max_outstanding = 1, min_size = 3, max_size = 5, first_size = 0;
        uint64_t t0 = 100;
        unsigned issued = 0, answered = 0;
        std::vector<uint64_t> accept_cycles;   // of each request's first beat
        Link &L() const { return *m_links[0].link; }
        void offer(uint64_t t) override
        {
            if (t < t0) return;
            if (!tx.msg && issued < n && issued - answered < max_outstanding)
            {
                if (busy.empty()) busy.assign(max_outstanding, false);
                uint64_t src = 0;
                while (busy[size_t(src)]) src++;
                unsigned size = (issued == 0 && first_size) ? first_size : min_size + unsigned(rng()() % (max_size - min_size + 1));
                OpA op = (rng()() & 1) ? OpA::Get : OpA::PutFullData;
                uint64_t addr = (rng()() % 64) << 5;
                tx.start(newRequest(*this, L(), op, size, addr, src));
                busy[size_t(src)] = true;
                issued++;
            }
            tx.offer(L().chan(Channel::A));
        }
        void accept(uint64_t) override { L().chan(Channel::D).setReady(true); }
        void commit(uint64_t t) override
        {
            const Beat &a = L().chan(Channel::A).accepted();
            if (a.msg && a.index == 0) accept_cycles.push_back(t);
            tx.commit(L().chan(Channel::A));
            const Beat &b = L().chan(Channel::D).accepted();
            if (b.msg && b.last()) { answered++; busy[size_t(b.msg->source)] = false; }
        }
        bool idle() const override { return answered >= n; }
    private:
        Tx tx; std::vector<bool> busy;
    };
}
#endif
