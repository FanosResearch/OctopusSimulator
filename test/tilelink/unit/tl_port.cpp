// TL-PORT-*: the TLPort contract (docs/TileLink_Plan.md s5.2) through the real crossbar, with an
// "echo" stand-in for the protocols: a handler-less master harness pushes TileLink-tagged Messages
// through TLPort -> Xbar -> TLPort to a slave harness that answers them (AccessAck[Data] for
// Get/PutFullData, GrantData for AcquireBlock, ProbeAck for a Probe). Every link carries a monitor
// in fatal mode unless a test says otherwise, so a non-conforming beat ends the run with status 2.
#include "tl_common.h"
using namespace octopus;
using namespace tltest;

TEST("TL-PORT-ROUNDTRIP")
{
    Sys s({{"w", 8}});
    TLPort *m = s.port(0), *sl = s.port(10);
    // a Get of a whole line at an unaligned address: the wire address is the line, the far end sees
    // the Octopus address unchanged; the requester's source tag comes back on the response
    Message g = get(500, 0x1008, 7);
    g.owner = 3; g.cycle = 1234; g.complementary_value = 42; g.complementary_value2 = 43;
    CHECK(m->pushMessage(g));
    Message req;
    CHECK(s.runUntil([&] { return sl->peekMessage(&req); }, 50));
    CHECK_EQ((int)req.tl.channel, 1); CHECK_EQ((int)req.tl.opcode, (int)tl::OpA::Get); CHECK_EQ((int)req.tl.size, 6);
    CHECK_EQ(req.msg_id, (uint64_t)500); CHECK_EQ((int)req.kind, (int)Message::K_GETS); CHECK_EQ(req.addr, (uint64_t)0x1008);
    CHECK_EQ((int)req.owner, 3); CHECK_EQ(req.cycle, (uint64_t)1234); CHECK_EQ(req.complementary_value, (uint64_t)42);
    CHECK_EQ(req.complementary_value2, (uint64_t)43); CHECK_EQ((int)req.from, 0); CHECK(req.data == NULL);
    CHECK_EQ(req.tl.source, (uint32_t)0);                         // the pool's id, in the crossbar's namespace (L0 base 0)
    sl->popFrontMessage();
    Message d = answer(req, Message::K_FILL, pattern(0x30));
    CHECK(sl->pushMessage(d));
    Message resp;
    CHECK(s.runUntil([&] { return m->peekMessage(&resp); }, 50));
    CHECK_EQ((int)resp.tl.channel, 4); CHECK_EQ((int)resp.tl.opcode, (int)tl::OpD::AccessAckData);
    CHECK_EQ(resp.tl.source, (uint32_t)7);                        // the requester's own tag restored
    CHECK_EQ(resp.msg_id, (uint64_t)500); CHECK_EQ((int)resp.kind, (int)Message::K_FILL);
    CHECK(resp.data != NULL); CHECK_EQ((int)resp.data_size, 64);
    CHECK(sameBytes(resp.data, pattern(0x30)));
    CHECK_EQ(m->idsInUse(1), 0);
    m->popFrontMessage();
    // a PutFullData's 64 bytes arrive at the slave as sent; the AccessAck carries no data
    Message p = put(501, 0x2000, 9, pattern(0x80));
    CHECK(m->pushMessage(p));
    CHECK(s.runUntil([&] { return sl->peekMessage(&req); }, 50));
    CHECK_EQ((int)req.tl.opcode, (int)tl::OpA::PutFullData);
    CHECK(req.data != NULL); CHECK(sameBytes(req.data, pattern(0x80)));
    sl->popFrontMessage();
    CHECK(send(sl, answer(req, Message::K_MEM_WACK, {})));
    CHECK(s.runUntil([&] { return m->peekMessage(&resp); }, 50));
    CHECK_EQ((int)resp.tl.opcode, (int)tl::OpD::AccessAck); CHECK(resp.data == NULL); CHECK_EQ(resp.tl.source, (uint32_t)9);
    CHECK_EQ(s.ic->monitorViolations(), (uint64_t)0);
    CHECK_EQ(s.ic->inFlight(), (size_t)0);
}

TEST("TL-PORT-MULTIBEAT")
{
    // w = 8: a 64-byte PutFullData is 8 beats. The master offers beat k at cycle k, the crossbar
    // forwards it one cycle later, so the last beat reaches the slave at cycle 8: the message is
    // visible after 9 steps and not after 8 (delivery on the LAST beat)
    for (int w : {8, 64})
    {
        Sys s({{"w", w}});
        TLPort *m = s.port(0), *sl = s.port(10);
        CHECK(send(m, put(600, 0x4000, 1, pattern(0x11))));
        int beats = 64 / w;
        Message req;
        for (int k = 0; k < beats; k++) { s.ic->step(); CHECK_MSG(!sl->peekMessage(&req), "visible before its last beat, w=" + std::to_string(w)); }
        s.ic->step();
        CHECK_MSG(sl->peekMessage(&req), "not visible after its last beat, w=" + std::to_string(w));
        CHECK(sameBytes(req.data, pattern(0x11)));
    }
}

TEST("TL-PORT-BACKPRESSURE")
{
    // the slave's A queue holds one message and the slave does not pop: the second Get waits on
    // the wire (ready LOW), the master's two-deep TX queue fills, the next push is refused
    Sys s({{"rx_depth_A", 1}, {"tx_depth_A", 2}, {"w", 64}});
    TLPort *m = s.port(0), *sl = s.port(10);
    CHECK_EQ(m->txFreeSlotsCh(1), 2);
    m->txReserveCh(1, 1); CHECK_EQ(m->txFreeSlotsCh(1), 1); m->txReleaseCh(1, 1);
    CHECK(send(m, get(700, 0x100, 0)));
    CHECK(send(m, get(701, 0x140, 1)));
    CHECK_EQ(m->txFreeSlotsCh(1), 0);
    CHECK(!send(m, get(702, 0x180, 2)));
    CHECK_EQ(m->refused(), (uint64_t)1);
    s.run(20);
    CHECK_EQ(sl->rxSize(1), 1);                                   // full: nothing more is accepted
    CHECK(send(m, get(702, 0x180, 2)));                  // the master's TX drained into the crossbar
    s.run(20);
    CHECK_EQ(sl->rxSize(1), 1);
    CHECK_EQ(s.ic->kernel().link("S10")->chan(tl::Channel::A).stats().stall > 0, true);
    // draining the slave lets the rest through, in order
    std::vector<uint64_t> got;
    Message req;
    for (int t = 0; t < 60 && got.size() < 3; t++)
    {
        s.ic->step();
        if (sl->peekMessage(&req)) { got.push_back(req.msg_id); sl->popFrontMessage(); }
    }
    CHECK_EQ(got.size(), (size_t)3);
    CHECK_EQ(got[0], (uint64_t)700); CHECK_EQ(got[1], (uint64_t)701); CHECK_EQ(got[2], (uint64_t)702);
}

TEST("TL-PORT-SOURCES")
{
    // o = 1: two sources. Two Gets take them, the third is refused although the TX queue has room;
    // the responses free them (each restoring its own tag) and the pool is reused lowest-first
    Sys s({{"o", 1}, {"w", 64}});
    TLPort *m = s.port(0), *sl = s.port(10);
    CHECK_EQ(m->poolSize(1), 2); CHECK_EQ(m->poolSize(3), 2);
    CHECK_EQ(m->txFreeIdsCh(1), 2);
    m->txReserveIdsCh(1, 1); CHECK_EQ(m->txFreeIdsCh(1), 1); m->txReleaseIdsCh(1, 1);
    CHECK(send(m, get(800, 0x100, 100)));
    CHECK(send(m, get(801, 0x140, 101)));
    CHECK_EQ(m->txFreeIdsCh(1), 0); CHECK_EQ(m->idsInUse(1), 2);
    CHECK(!send(m, get(802, 0x180, 102)));
    std::vector<uint32_t> wire_src, tags;
    Message req, resp;
    for (int t = 0; t < 80 && tags.size() < 2; t++)
    {
        s.ic->step();
        if (sl->peekMessage(&req)) { wire_src.push_back(req.tl.source); sl->popFrontMessage(); CHECK(send(sl, answer(req, Message::K_FILL, pattern(1)))); }
        if (m->peekMessage(&resp)) { tags.push_back(resp.tl.source); m->popFrontMessage(); }
    }
    CHECK_EQ(wire_src.size(), (size_t)2); CHECK_EQ(wire_src[0], (uint32_t)0); CHECK_EQ(wire_src[1], (uint32_t)1);
    CHECK_EQ(tags.size(), (size_t)2); CHECK_EQ(tags[0], (uint32_t)100); CHECK_EQ(tags[1], (uint32_t)101);
    CHECK_EQ(m->idsInUse(1), 0);
    CHECK(send(m, get(802, 0x180, 102)));
    CHECK(s.runUntil([&] { return sl->peekMessage(&req); }, 50));
    CHECK_EQ(req.tl.source, (uint32_t)0);                         // reused
}

TEST("TL-PORT-PRIORITY")
{
    // one receive queue per channel, served E, D, C, B, A whatever the arrival order
    Sys s({});
    TLPort *m = s.port(0), *sl = s.port(10);
    auto tagged = [](uint64_t id, int ch) { Message x = Message(id, 0x40 * id); x.tl.channel = (uint8_t)ch; return x; };
    for (int ch : {1, 3, 5, 1, 3}) { Message x = tagged(10 + ch, ch); CHECK(sl->pushMessage2RX(x)); }
    std::vector<int> order;
    Message x;
    while (sl->peekMessage(&x)) { order.push_back(x.tl.channel); sl->popFrontMessage(); }
    CHECK_EQ(order.size(), (size_t)5);
    CHECK(order == std::vector<int>({5, 3, 3, 1, 1}));
    for (int ch : {2, 4, 2}) { Message y = tagged(20 + ch, ch); CHECK(m->pushMessage2RX(y)); }
    order.clear();
    while (m->peekMessage(&x)) { order.push_back(x.tl.channel); m->popFrontMessage(); }
    CHECK(order == std::vector<int>({4, 2, 2}));
    // the MessageType mapping for callers that are not TileLink-aware
    CHECK_EQ(m->channelOf(MessageType::REQUEST, true), 1);
    CHECK_EQ(m->channelOf(MessageType::DATA_RESPONSE, true), 3);
    CHECK_EQ(m->channelOf(MessageType::SERVICE_REQUEST, false), 2);
    CHECK_EQ(m->channelOf(MessageType::DATA_RESPONSE, false), 4);
    CHECK_EQ(sl->channelOf(MessageType::SERVICE_REQUEST, true), 2);
    CHECK_EQ(sl->channelOf(MessageType::DATA_RESPONSE, true), 4);
    CHECK_EQ(sl->channelOf(MessageType::REQUEST, false), 1);
    CHECK_EQ(sl->channelOf(MessageType::DATA_RESPONSE, false), 3);
    // the rx depth bounds pushMessage2RX too
    Sys t({{"rx_depth_A", 1}});
    Message a1 = tagged(1, 1), a2 = tagged(2, 1);
    CHECK(t.port(10)->pushMessage2RX(a1)); CHECK(!t.port(10)->pushMessage2RX(a2));
    CHECK(!t.port(10)->canAcceptRX(MessageType::REQUEST));
}

TEST("TL-PORT-GRANT")
{
    // TL-C: AcquireBlock -> GrantData (the slave's sink pool) -> GrantAck (frees the sink, the
    // slave's own tag restored). Monitors on, fatal: the exchange is conforming.
    Sys s({{"w", 8}, {"i", 1}});
    TLPort *m = s.port(0), *sl = s.port(10);
    Message acq = get(900, 0x3000, 5); acq.tl.opcode = (uint8_t)tl::OpA::AcquireBlock; acq.tl.param = (uint8_t)tl::Grow::NtoT;
    CHECK(m->pushMessage(acq));
    Message req, resp;
    CHECK(s.runUntil([&] { return sl->peekMessage(&req); }, 50));
    sl->popFrontMessage();
    CHECK_EQ(sl->poolSize(4), 2); CHECK_EQ(sl->txFreeIdsCh(4), 2);
    Message g = answer(req, Message::K_RESP, pattern(0x55));
    g.tl.opcode = (uint8_t)tl::OpD::GrantData; g.tl.param = (uint8_t)tl::Cap::toT; g.tl.sink = 55;
    CHECK(sl->pushMessage(g));
    CHECK_EQ(sl->idsInUse(4), 1); CHECK_EQ(sl->txFreeIdsCh(4), 1);
    CHECK(s.runUntil([&] { return m->peekMessage(&resp); }, 80));
    m->popFrontMessage();
    CHECK_EQ((int)resp.tl.opcode, (int)tl::OpD::GrantData); CHECK_EQ(resp.tl.source, (uint32_t)5);
    CHECK(sameBytes(resp.data, pattern(0x55)));
    Message e(900, 0x3000); e.kind = Message::K_GRANT_ACK; e.tl.channel = 5; e.tl.sink = resp.tl.sink; e.to = {10};
    CHECK(m->pushMessage(e));
    CHECK(s.runUntil([&] { return sl->peekMessage(&req); }, 50));
    CHECK_EQ((int)req.tl.channel, 5); CHECK_EQ(req.tl.sink, (uint32_t)55); CHECK_EQ((int)req.kind, (int)Message::K_GRANT_ACK);
    CHECK_EQ(sl->idsInUse(4), 0);
    CHECK_EQ(s.ic->monitorViolations(), (uint64_t)0);
}

TEST("TL-PORT-PROBE")
{
    // a B to two masters fans out into one message each, its source the base of that master's
    // range (o = 4: L0 at 0, L1 at 16); each answers a ProbeAck with the source it saw, which the
    // crossbar routes back by address and remaps into the slave's namespace
    Sys s({{"o", 4}}, {0, 1}, /*monitor=*/0);
    TLPort *sl = s.port(10);
    Message b(950, 0x5000); b.kind = Message::K_INV; b.tl.channel = 2; b.tl.opcode = (uint8_t)tl::OpB::ProbeBlock;
    b.tl.param = (uint8_t)tl::Cap::toN; b.tl.size = 6; b.to = {0, 1};
    CHECK_EQ(sl->txFreeSlotsCh(2), 4);
    CHECK(sl->pushMessage(b));
    CHECK_EQ(sl->txSize(2), 2);
    std::vector<uint32_t> back;
    Message x;
    for (int t = 0; t < 60 && back.size() < 2; t++)
    {
        s.ic->step();
        for (int id : {0, 1})
            if (s.port(id)->peekMessage(&x))
            {
                CHECK_EQ((int)x.tl.channel, 2); CHECK_EQ(x.tl.source, (uint32_t)0); CHECK_EQ(x.msg_id, (uint64_t)950);
                s.port(id)->popFrontMessage();
                Message a(950, 0x5000); a.kind = Message::K_ACK; a.tl.channel = 3; a.tl.opcode = (uint8_t)tl::OpC::ProbeAck;
                a.tl.param = (uint8_t)tl::Report::NtoN; a.tl.size = 6; a.tl.source = x.tl.source; a.to = {10};
                CHECK(s.port(id)->pushMessage(a));
            }
        if (sl->peekMessage(&x)) { back.push_back(x.tl.source); sl->popFrontMessage(); }
    }
    CHECK_EQ(back.size(), (size_t)2);
    std::sort(back.begin(), back.end());
    CHECK_EQ(back[0], (uint32_t)0); CHECK_EQ(back[1], (uint32_t)16);
}

TEST("TL-PORT-ELAB")
{
    // the derived widths hold every master's sources and every slave's sinks (TileLink s6.4); a
    // TL-UL link has no B..E, so its master sends only A and receives only D
    Sys s({{"o", 3}, {"i", 2}}, {0, 1, 2});
    const tl::LinkParams &mp = s.ic->kernel().link("L0")->params(), &sp = s.ic->kernel().link("S10")->params();
    CHECK_EQ((int)sp.o, 5);                                       // 3 masters x 8 sources = 24 ids -> 5 bits
    CHECK_EQ((int)mp.i, 2);
    CHECK_EQ((int)mp.o, 3); CHECK_EQ((int)sp.i, 2);
    Sys u({}, {0}, 1, "UL");
    CHECK(u.port(0)->sends(1)); CHECK(!u.port(0)->sends(3)); CHECK(!u.port(0)->receives(2));
    CHECK_EQ(u.port(0)->txFreeSlotsCh(3), 0);
    // the clock: no controllers to derive from, half the default core period; the kernel steps on
    // the falling edge, once per two edges (one TileLink cycle per core cycle, plan s3.2)
    CHECK_EQ(u.ic->getClkPeriod(), (uint64_t)50);
    uint64_t c0 = u.ic->cycle();
    ClockedObj *clk = u.ic;
    for (int k = 0; k < 4; k++) clk->cycleProcess();
    CHECK_EQ(u.ic->cycle(), c0 + 2);
}

TEST("TL-PORT-TRACE")
{
    // s5.3 I-A accounting: beat 0 accepted at the sender logs ENTER, the last beat accepted at the
    // receiver logs EXIT; A as REQ_BUS, D as RESP_BUS; comp = the channel, flags = the destination.
    // Runs last: it closes the trace file (OCTOPUS_TRACE, set by main) to read it back.
    Sys s({{"w", 8}});
    TLPort *m = s.port(0), *sl = s.port(10);
    CHECK(send(m, get(9999, 0x7000, 3)));
    Message req, resp;
    CHECK(s.runUntil([&] { return sl->peekMessage(&req); }, 50));
    sl->popFrontMessage();
    CHECK(send(sl, answer(req, Message::K_FILL, pattern(2))));
    CHECK(s.runUntil([&] { return m->peekMessage(&resp); }, 50));
    Logger::getLogger()->traceFinalize();
    std::vector<TraceRec> recs = readTrace(traceFile());
    CHECK_MSG(!recs.empty(), "no trace records in " + traceFile());
    int n_req_enter = 0, n_req_exit = 0, n_resp_enter = 0, n_resp_exit = 0;
    for (auto &r : recs)
    {
        if (r.msg_id != 9999) continue;
        if (r.resource == (uint8_t)Logger::Role::REQ_BUS)
        {
            CHECK_EQ((int)r.comp, 1); CHECK_EQ((int)r.flags, 10); CHECK_EQ((int)r.kind, (int)Message::K_GETS);
            (r.phase == (uint8_t)Logger::Phase::ENTER ? n_req_enter : n_req_exit)++;
        }
        if (r.resource == (uint8_t)Logger::Role::RESP_BUS)
        {
            CHECK_EQ((int)r.comp, 4); CHECK_EQ((int)r.flags, 0); CHECK_EQ((int)r.kind, (int)Message::K_FILL);
            (r.phase == (uint8_t)Logger::Phase::ENTER ? n_resp_enter : n_resp_exit)++;
        }
    }
    CHECK_EQ(n_req_enter, 1); CHECK_EQ(n_req_exit, 1); CHECK_EQ(n_resp_enter, 1); CHECK_EQ(n_resp_exit, 1);
}
