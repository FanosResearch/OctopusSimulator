/*
 * File  :      ScriptedSlave.h
 * Test fixture: a slave with a per-cycle ready schedule on the channels it receives (A, C, E) and
 * scripted offers on the channels it sends (B, D), explicit or adaptive as in ScriptedMaster.h.
 * Optionally it answers requests by itself (`autoRespond`): when a request's last beat is accepted
 * at t, the response built by `respond` (default: the legal response of Tables 12/13 with param
 * 0, the request's size and source, data zero) is sent adaptively from t + delay; `tweak` may
 * edit it first (a wrong opcode, param, size, denied, corrupt, data), so the mutants of
 * docs/TileLink_Negative_Tests.md §1.1 are scripted by exception.
 */
#ifndef _TL_TEST_SCRIPTED_SLAVE_H
#define _TL_TEST_SCRIPTED_SLAVE_H
#include "ScriptedMaster.h"

namespace octopus { namespace tl { namespace fixture
{
    class ScriptedSlave : public Agent, public ScriptEngine
    {
    public:
        explicit ScriptedSlave(const std::string &n) : Agent(n) {}
        Level capability(const Link &, Role) const override { return Level::C; }
        void offer(uint64_t t) override;
        void accept(uint64_t t) override;
        void commit(uint64_t t) override;
        bool idle() const override { return autosDone(); }
        Link &link() const;

        bool autoRespond = false;
        unsigned delay = 1;                                         // >= 1 (plan §3.2)
        std::function<MsgPtr(const Msg &req)> respond;              // null: the default response
        std::function<void(Msg &resp, const Msg &req)> tweak;       // edit the response
        MsgPtr defaultResponse(const Msg &req);
    };
}}}
#endif /* _TL_TEST_SCRIPTED_SLAVE_H */
