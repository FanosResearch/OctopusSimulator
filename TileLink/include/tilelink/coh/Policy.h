/*
 * File  :      Policy.h
 * The TL-C policy engine (docs/TileLink_Plan.md §3.7): pure logic over a coh::Table, shared by the
 * standalone agents (CacheMaster, CoherentManager) and, later, Octopus's TLC_L1 / TLC_LLC handlers.
 *   - decode: a received TileLink message (plus, at the manager, the directory's view of the block)
 *     -> the table's event, per the decode tables of docs/TileLink_TLC_Tables.md §2.2 (L1) and §3.2
 *     (manager), including the param-class collapsing of §3.2 (ProbeAck TtoN/BtoN/NtoN = toN);
 *   - actions: each action id of the table classified once (what it emits, with which param), per
 *     §2.3 and §3.3, so that executing a cell is a switch on a small enum;
 *   - builders: the TileLink message header an action emits (opcode, param per §6, size = block,
 *     source/sink rules of §7: a ProbeAck copies the Probe's source and address, a GrantAck the
 *     Grant's sink, a Grant/AccessAck the request's source and size).
 * No kernel, no agent: unit-testable on its own (test/coh).
 *
 * Decode faults are returned as Decoded::fault (the event is -1); the agent turns them into a
 * fatal error (TLC_Tables §1.2 rule 1). A table cell that is `Fault/` is the agent's business too.
 */
#ifndef _TL_COH_POLICY_H
#define _TL_COH_POLICY_H
#include "tilelink/Msg.h"
#include "tilelink/coh/Table.h"
#include <string>
#include <vector>

namespace octopus { namespace tl { namespace coh
{
    struct Decoded
    {
        int event = -1;
        std::string fault;     // non-empty: a decode fault (TLC_Tables §1.2 rule 1)
        bool ok() const { return event >= 0; }
    };

    // One executed cell, for tests and the debug trace (CacheMaster / CoherentManager onTransition):
    // "<cycle> <agent> 0x<block> <from> + <event> -> <cell> -> <to> [<message>]".
    struct Transition
    {
        uint64_t    cycle = 0;
        std::string agent;
        uint64_t    block = 0;
        std::string from, event, cell, to;
        std::string msg;          // Msg::describe() of the consumed message ("" for CPU / self events)
        int         client = -1;  // manager: the client the consumed message came from
        bool        stalled = false;   // a Stall cell (reported once per parked message and state)
        std::string line() const;
    };

    // ------------------------------------------------------------------ L1 master (TLC_Tables §2)
    enum class L1Op : uint8_t { Stall, Hit, Fault, Error, AcquireBlock, GrantAck, ProbeAck, ProbeAckData, Release, ReleaseData };
    struct L1Action
    {
        L1Op    op = L1Op::Fault;
        uint8_t param = 0;     // Grow (Acquire), Prune or Report (ProbeAck, Release)
    };
    const char *l1OpName(L1Op op);

    class L1Policy
    {
    public:
        // Resolves every event and action the engine needs; ConfigError on a missing event or an
        // action the engine does not implement.
        explicit L1Policy(const Table &t);
        const Table &table() const { return m_t; }

        int eventLoad() const { return m_ev_load; }
        int eventStore() const { return m_ev_store; }
        int eventReplacement() const { return m_ev_repl; }
        int stateInvalid() const { return m_st_I; }   // the state of an absent line

        // §2.2: Load / Store from the CPU (complementary_value 0 / 1), Replacement from the
        // controller's victim selection.
        Decoded decodeCpu(bool store) const { Decoded d; d.event = store ? m_ev_store : m_ev_load; return d; }
        Decoded decodeReplacement() const { Decoded d; d.event = m_ev_repl; return d; }
        // §2.2: a B message (ProbeBlock, param = Cap) or a D message (Grant, GrantData,
        // ReleaseAck). Faults: ProbePerm and the B Access/Hint messages (§9), Grant[Data] toN
        // (plan §8 decision 7), any other D opcode (the L1 issues no uncached access).
        Decoded decode(const MsgHeader &m) const;

        const L1Action &action(int id) const { return m_actions.at(size_t(id)); }

        // §2.3, §6: the header of the message action `id` emits. `trigger` is the consumed message
        // (the Probe a ProbeAck answers: its source and address are copied, §9.3.5 "c_source
        // copied from b_source"; the Grant a GrantAck answers: its sink is copied, §9.3.9);
        // `source` is the source allocated for an Acquire (A namespace) or a Release (C namespace).
        // Returns false for an action that emits no TileLink message (Stall, Hit, Fault, Error).
        bool build(int id, uint64_t block, unsigned lg_block, const MsgHeader *trigger, uint64_t source, MsgHeader &out) const;

    private:
        Table m_t;
        std::vector<L1Action> m_actions;
        int m_ev_load, m_ev_store, m_ev_repl;
        int m_ev_probe[3];          // indexed by Cap: toT, toB, toN
        int m_ev_grant[2];          // Cap toT, toB
        int m_ev_grantdata[2];
        int m_ev_denied, m_ev_relack;
        int m_st_I;
    };

    // ------------------------------------------------------------------ manager (TLC_Tables §3)
    enum class MgrOp : uint8_t
    {
        Stall, Fault, GetOut, PutFullDataOut, ForwardOut, ForwardIn, SaveData, Drop,
        GrantData, Grant, GrantDenied, ReleaseAck, ProbeAll, ProbeOwner, SetOwner, AddSharer,
        OwnerToSharer, RemoveHolder, DecCount, Execute, AccessAckDataIn, HintAck, HintAckDenied,
        PutFullDataDirtyOut   // PutFullData_out only if the manager's copy is dirty (docs/TileLink_LLC_Dirty_Writeback_Plan.md)
    };
    struct MgrAction
    {
        MgrOp   op = MgrOp::Fault;
        uint8_t param = 0;     // Cap of a Grant or a Probe
    };
    const char *mgrOpName(MgrOp op);
    // True for the actions that answer a request (TLC_Tables §1.2 rule 3: Grant*, AccessAckData_in,
    // Execute, ForwardIn, HintAck*).
    bool answers(MgrOp op);

    // What the directory says about the sender of the message being decoded (§3.2).
    enum class Holder : uint8_t { None = 0, Branch = 1, Tip = 2 };
    struct DirView
    {
        Holder   sender = Holder::None;   // the sender's listing for the block
        unsigned holders = 0;             // owner + sharers listed
        unsigned probes = 0;              // the probe counter (Probes sent, not yet answered; §1.2 rule 4)
    };

    class ManagerPolicy
    {
    public:
        explicit ManagerPolicy(const Table &t);
        const Table &table() const { return m_t; }
        int stateN() const { return m_st_N; }         // the state of a block with no directory entry
        int eventReplacement() const { return m_ev_repl; }
        int eventGrantAck() const { return m_ev_gack; }

        // §3.2, inner A: AcquireBlock by param and the requester's listing; Get_u; PutAtomic_u
        // (PutFullData, PutPartialData, ArithmeticData, LogicalData); Intent by param. Faults:
        // AcquirePerm (§9: not supported), Intent params 2..4, NtoB/NtoT from a listed holder
        // and BtoT from the listed Tip. The last two are deferred while the block has Probes
        // outstanding (see Policy.cpp: the A message overtook the sender's own ProbeAck on C;
        // every probing state Stalls inner A, and the parked message is re-decoded after the
        // ProbeAck removed the sender).
        Decoded decodeA(const MsgHeader &m, const DirView &d) const;
        // §3.2, inner C: ProbeAck[Data] by param class and the counter (the last_ event when the
        // counter is 1 before the decrement, §1.2 rule 4); Release[Data] by param and the sender.
        Decoded decodeC(const MsgHeader &m, const DirView &d) const;
        // §3.2, inner E (the agent matches the sink to the block's unanswered Grant).
        Decoded decodeE() const { Decoded d; d.event = m_ev_gack; return d; }
        // §3.2, outer D: AccessAckData / AccessAck, denied or not. (The AccessAck of an
        // eviction write-back never reaches the table, §1.2 rule 7: the agent's port eats it.)
        Decoded decodeOuterD(const MsgHeader &m) const;
        Decoded decodeReplacement() const { Decoded d; d.event = m_ev_repl; return d; }

        const MgrAction &action(int id) const { return m_actions.at(size_t(id)); }
        // §1.2 rule 8: the cap a denied Grant carries = the cap the Acquire asked for.
        static uint8_t requestedCap(const MsgHeader &acquire);

        // §3.3 builders. `lg_block` is log2 of the block size.
        static MsgHeader probe(uint64_t block, unsigned lg_block, uint8_t cap, uint64_t source);
        // GrantData / Grant toT|toB (denied 0) and the denied Grant (opcode Grant, denied 1, param
        // the requested cap): source and size copied from the Acquire, the new sink.
        static MsgHeader grant(const MgrAction &a, const MsgHeader &acquire, uint64_t sink);
        static MsgHeader releaseAck(const MsgHeader &release);
        // AccessAck (Put), AccessAckData (Get, Atomic), HintAck (Intent): source, size, address
        // copied from the request (D has no address: the request's, simulator-only).
        static MsgHeader accessAck(const MsgHeader &request, bool denied);
        static MsgHeader outerGet(uint64_t block, unsigned lg_block);
        static MsgHeader outerPut(uint64_t block, unsigned lg_block);

    private:
        Table m_t;
        std::vector<MgrAction> m_actions;
        int m_st_N;
        int m_ev_ntob, m_ev_ntot, m_ev_btot, m_ev_btot_last, m_ev_btot_non;
        int m_ev_pa_n, m_ev_last_pa_n, m_ev_pad_n, m_ev_pa_b, m_ev_pad_b;
        int m_ev_gack, m_ev_rel_b, m_ev_last_rel_b, m_ev_rel_t, m_ev_reld_t, m_ev_repl;
        int m_ev_aad, m_ev_aad_den, m_ev_aa, m_ev_aa_den, m_ev_get, m_ev_put;
        int m_ev_prefetch, m_ev_inval, m_ev_clean, m_ev_flush;
    };
}}}
#endif /* _TL_COH_POLICY_H */
