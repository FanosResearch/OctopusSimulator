/*
 * File  :      TLC_LLC.h
 * The TL-C inclusive manager protocol handler (docs/TileLink_Plan.md s5.3 I-A, step T7): an Octopus LLC
 * (or LLC bank) that is the TileLink slave of the L1s on interconnect_type TileLink, executing the
 * library's manager table (TileLink/tables/TLC_MESI_LLC.csv, or TLC_MSI_LLC.csv for MSI: fsm_filename
 * picks it; docs/TileLink_TLC_Tables.md s3) with the library's policy engine (tl::coh::ManagerPolicy:
 * decode with the directory's view, action classification, the Probe / Grant / ReleaseAck / outer
 * Get and Put builders), exactly as the standalone CoherentManager does.
 *
 * protocol_type TLC_LLC; cache_controller_type CacheControllerDirectory (see TLC_L1.h).
 *
 * The directory, per block (TLC_Tables s3.1): the Tip (owner), the Branches (sharers), the probe
 * counter (Probes sent and not yet answered, rule 4), the parked request (rule 3) and the
 * unanswered Grant (its sink, matched by the GrantAck). The block's table state is the line's state
 * in the data handler, as for every Octopus protocol (array, MSHR or write-back buffer); an absent
 * line is N. Holders are clients: an L1 is identified by the endpoint that sent its message
 * (Message::from, stamped by its TLPort), which is the owner of one source range of the crossbar's
 * namespace (TileLinkInterconnect::endpointOfSource), so it is the library's "client by source
 * range"; a Probe names its targets in Message::to and the TLPort gives each copy the base source of
 * that master.
 *
 * Engine rules (TLC_Tables s1.2) in Octopus's controller:
 *   1. Decode: inner A (AcquireBlock by param and the sender's listing, with the T6 exception for a
 *      sender whose ProbeAck is still outstanding), inner C (ProbeAck[Data] by param class and the
 *      counter, Release[Data] by param and sender), inner E (the GrantAck, its sink checked against the
 *      block's unanswered Grant), outer D (AccessAckData / AccessAck from Message::tl when the memory
 *      side speaks TileLink; an untagged K_FILL / K_FILL_ROLLBACK is AccessAckData, so the
 *      point-to-point memory bus works unchanged), the self-raised Replacement. A decode fault is fatal.
 *   2. Stall parks the message in the processing queue (getRequestState NonReady); per line, demand
 *      requests (the Acquires, complementary_value 0 / 1) keep their arrival order through the queue's
 *      FCFS gate (TLC_Tables s4 invariant 6: one transaction per block, in arrival order).
 *   3. An inner-A cell that does not answer parks its request: in this handler (for the cells that
 *      follow) and in the controller's pending list (ADD_PENDING: the MSHR count and the Logger). The
 *      answering cell's Grant is ControllerAction::TL_RESPOND, which removes it from that list.
 *   4. ProbeBlock_toN_all sets the counter to the Probes sent (every holder but the requester),
 *      ProbeBlock_to*_owner to 1; DecCount on every ProbeAck; last_ when the counter is 1.
 *   5. SetOwner / AddSharer / OwnerToSharer / RemoveHolder as the rule says (the requester is the
 *      consumed Acquire's sender, else the parked one's; RemoveHolder removes the consumed C's sender).
 *   6. Data: SaveData writes the consumed bytes into the line (UPDATE_CACHE_LINE with the data: the
 *      fill from the MSHR, or the line in the array or the write-back buffer); a GrantData or
 *      PutFullData of a cell that saved data carries those bytes, otherwise it reads the line (with the
 *      data array's timing, CacheController::checkReadinessOfCache). The LLC allocates on the fill and
 *      evicts as CacheDataHandler_COTS does: the fill takes the victim's way and the victim waits in the
 *      write-back buffer for its Replacement (CacheController::checkReplacements), whose cell probes
 *      its holders (BN_e, TN_e) or writes it back (I).
 *   7. An eviction's PutFullData is a port transaction: WRITE_BACK records it in the controller's write
 *      table (BaseController::noteMemWrite, hold H1: a later read of the line is filled from there) and
 *      its AccessAck (K_MEM_WACK) is consumed at the intake (consumeMemAck), never a table event.
 *   8. Grant_denied carries the requested cap (ManagerPolicy::grant).
 *   Resources (plan s5.2): predictEmissions returns the cell's emissions (TLC_Tables s7.2) with the
 *   probe fan-out resolved to the holders it will probe; CacheController reserves the inner B / D
 *   slots, the Grant's sink and the outer A slot and source before the message is popped.
 *
 * Messages (TLC_Tables s8): ProbeBlock toB to the Tip K_FWD; toN to the Tip K_FWD when serving a
 * request, K_INV for the Replacement; toN to every Branch K_INV; Grant[Data] K_RESP; ReleaseAck K_ACK;
 * Get K_MEM_READ; PutFullData K_MEM_WRITE. Message::tl on every one (the memory messages too, the
 * contract with the memory side: Get / PutFullData on A, size log2(block)); a Grant copies the
 * Acquire's tl.source and a ReleaseAck the Release's; every message carries the msg_id of the request
 * it serves (the Acquire's for its Probes, Grant and memory read; the Replacement's for an eviction).
 * The uncached accesses and CMOs of the table (Get_u, PutAtomic_u, Intent_*) are never produced by
 * Octopus's L1 (TLC_Tables s8); their actions (ForwardOut, ForwardIn, Execute, AccessAckData_in,
 * HintAck*, Drop) are refused here as unreachable and are exercised by tlsim.
 */

#ifndef _TLC_LLC_H
#define _TLC_LLC_H

#include "DirectoryProtocol.h"
#include "TLC_Common.h"
#include "tilelink/coh/Policy.h"
#include <map>
#include <memory>
#include <set>

namespace octopus
{
    class TLC_LLC : public DirectoryProtocol
    {
    public:
        TLC_LLC(CacheDataHandler *cache, const std::string &fsm_path, int id, int sharedMemId);
        ~TLC_LLC();

        virtual std::vector<ControllerAction> processRequest(Message &request_msg, DebugPrint *dprint = NULL) override;
        virtual FRFCFS_State getRequestState(const Message &msg, FRFCFS_State state) override;
        virtual void initializeCacheStates() override;   // the absent block is N (TLC_Tables s3.1)
        virtual void createDefaultCacheLine(uint64_t address, GenericCacheLine *cache_line) override;   // a preloaded line: I

        bool consumedBy(const std::string &event, MsgClass &out) const override;
        bool emittedBy(const std::string &action, std::vector<MsgClass> &out) const override;
        bool hasClassAnnotation() const override { return true; }
        bool predictEmissions(const Message &msg, GenericCacheLine &line, std::vector<MsgClass> &out) override;

        const tl::coh::ManagerPolicy &policy() const { return *m_policy; }
        // end of run (stderr, TileLink runs only): replacements, write-backs, fills, Grants, Probes
        void report() const;

    protected:
        struct Dir
        {
            int owner = -1;                 // the Tip, an L1's endpoint id
            std::set<int> sharers;          // the Branches
            unsigned probes = 0;            // Probes sent and not answered (rule 4)
            bool parked = false;            // rule 3: the request this block's transaction serves
            Message req;
            bool grant_out = false;         // a Grant sent, its GrantAck not yet consumed
            uint32_t grant_tag = 0;         // the Grant's tl.sink as the handler gave it (TLPort restores it on the GrantAck)
        };
        std::unique_ptr<tl::coh::ManagerPolicy> m_policy;
        std::map<uint64_t, Dir> m_dir;
        int m_stall_id = -1, m_save_id = -1;
        unsigned m_lg_block = 6;
        uint32_t m_next_tag = 0;
        struct Counters { uint64_t replacements = 0, writebacks = 0, fills = 0, grants = 0, probes = 0, releases = 0, acquires = 0, denied = 0; } m_c;
        // Test hook (docs/TileLink_Denied_Grant_Retry_Plan.md): OCTOPUS_TL_DENY_FILLS=<n> takes the first n
        // outer fills of this LLC as denied (AccessAckData_denied), the table's own path to a denied Grant
        // (NT_d -> Grant_denied/N_g). Decided once per fill (decode also runs for readiness), counted when
        // the fill is consumed. -1 = off.
        mutable long m_deny_left = -1;
        mutable std::set<std::pair<uint64_t, uint64_t>> m_deny_keys;   // (the fill's msg_id, line)

        uint64_t blockOf(uint64_t addr) const { return addr & ~uint64_t(m_data_handler->getBlockSize() - 1); }
        int  stateOf(uint64_t addr) const;
        const Dir *dir(uint64_t block) const { auto it = m_dir.find(block); return it == m_dir.end() ? nullptr : &it->second; }
        tl::coh::DirView view(uint64_t block, int sender) const;
        // the endpoint whose request the message serves now: the consumed Acquire's, else the parked one's
        int  requesterOf(const Message &msg) const;
        // the L1s a Probe action of this cell targets (TLC_Tables s3.3)
        std::vector<int> probeTargets(const tl::coh::MgrAction &a, uint64_t block, int requester) const;
        // TLC_Tables s3.2: the event a message decodes to; fatal on a decode fault
        int  decode(const Message &msg) const;
        bool isStall(int state, int event) const { return m_stall_id >= 0 && m_policy->table().hasAction(state, event, m_stall_id); }
        [[noreturn]] void fault(const std::string &why, const Message &msg, int state, int event) const;
    };
}

#endif /* _TLC_LLC_H */
