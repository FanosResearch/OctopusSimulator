/*
 * File  :      TLC_L1.h
 * The TL-C L1 protocol handler (docs/TileLink_Plan.md s5.3 I-A, step T7): an Octopus L1 that is a
 * TileLink master on interconnect_type TileLink, executing the library's L1 table
 * (TileLink/tables/TLC_MESI_L1.csv, shared by the MESI and MSI presets; docs/TileLink_TLC_Tables.md
 * s2) with the library's policy engine (tl::coh::L1Policy: decode, action classification, message
 * builders), exactly as the standalone CacheMaster does. Nothing here decides coherence: every
 * transition is a table cell, every emitted header is built by L1Policy::build.
 *
 * protocol_type TLC_L1, fsm_filename TLC_MESI_L1; cache_controller_type CacheControllerDirectory (its
 * intake filters by Message::to and computes readiness for messages without data; the handler is a
 * DirectoryProtocol only so that the controller's counters can be attached, it uses none of them).
 *
 * How the engine rules of TLC_Tables s1.2 map onto Octopus's controller:
 *   1. Decode (TLC_Tables s2.2): a CPU Load / Store (lower interface, complementary_value 0 / 1), the
 *      controller's Replacement (Message::Source::SELF, K_EVICT), a B or D message (upper
 *      interface) by its TileLink header through L1Policy::decode. A decode fault is fatal (status
 *      2), as LLCMSIDirectory::readEvent's "Invalid Transaction".
 *   2. Stall parks: getRequestState is NonReady on a Stall cell, so the message stays in the
 *      processing queue and is re-decoded when the line changes; a message the controller took as
 *      ready earlier (CacheControllerDirectory's intake decides once for upper-interface messages)
 *      and whose cell has become a Stall goes back to the queue through ControllerAction::STALL. The
 *      TLPort's receive queues keep accepting meanwhile (TLPort.h), so a Stall never withholds ready.
 *   3. Parking a request: a CPU access that leaves a stable state for a transient one is parked on
 *      the line (ADD_PENDING, as MSIDirectory's GetS / GetM); the Grant cell's Hit answers it
 *      (REMOVE_PENDING). Its Error (a denied Grant) answers nothing: the access stays parked and is
 *      replayed for a new Acquire (TL_RETRY_PENDING, CacheController::tlRetryPending), the CPU is
 *      answered once by the Grant that succeeds (docs/TileLink_Denied_Grant_Retry_Plan.md).
 *   Replacement: CacheController::checkReplacements raises it when a fill moves the victim to the
 *      write-back buffer; a victim a Probe has invalidated before the event runs is gone, and its
 *      Replacement is a no-op, as the library's CacheMaster treats an Evict of an absent line.
 *   6. Data: a GrantData that is not denied fills the line before the cell's actions run
 *      (UPDATE_CACHE_LINE with the bytes, CacheDataHandler_COTS's fill from the MSHR); ProbeAckData
 *      and ReleaseData carry the line (WRITE_BACK reads it); Hit serves the CPU from the line and a
 *      Store writes it (HIT_Action, MODIFY_DATA). An emission that reads the line runs before the
 *      state update when the cell leaves the line invalid (M + ProbeBlock_toN -> I), as MSIDirectory
 *      puts its write-backs first.
 *   Per-channel order (plan s5.2): the TLPort serves its receive queues E, D, C, B, A; the
 *   controller's queue keeps the arrival order per line for demand requests only (Acquires at the
 *   LLC, CPU accesses here), so a parked lower-priority message never blocks a higher one.
 *   Resources (plan s5.2 points 1-2): predictEmissions returns the cell's emissions per channel
 *   (TLC_Tables s7.1); CacheController::txAdmitTileLink reserves the C/A/E slots and the A and
 *   Release sources before the message is popped; a ProbeAck carries the Probe's source.
 *
 * Messages (TLC_Tables s8): AcquireBlock NtoB K_GETS, NtoT / BtoT K_GETM; GrantAck K_GRANT_ACK;
 * ProbeAck K_ACK; ProbeAckData K_WB_DATA; Release BtoN K_PUTS, TtoN K_PUTE; ReleaseData K_PUTM.
 * Message::tl is set on every emission; a ProbeAck copies the Probe's tl.source, a GrantAck the
 * Grant's tl.sink, and every message carries the msg_id of the request it belongs to (the CPU access
 * for an Acquire, the Grant's for its GrantAck, the Probe's for its ProbeAck, the Replacement's for a
 * Release), so the Logger correlates the stages (plan s5.3 I-A). Destinations: homeOf(line).
 */

#ifndef _TLC_L1_H
#define _TLC_L1_H

#include "DirectoryProtocol.h"
#include "TLC_Common.h"
#include "tilelink/coh/Policy.h"
#include <memory>

namespace octopus
{
    class TLC_L1 : public DirectoryProtocol
    {
    public:
        TLC_L1(CacheDataHandler *cache, const std::string &fsm_path, int id, int sharedMemId);
        ~TLC_L1();

        virtual std::vector<ControllerAction> processRequest(Message &request_msg, DebugPrint *dprint = NULL) override;
        virtual FRFCFS_State getRequestState(const Message &msg, FRFCFS_State state) override;
        // S, E, M give the core a readable copy, and SM_a keeps its Branch until the Grant (TLC_Tables s2.1)
        virtual bool isReadableState(int state) override;
        bool isCpuDemand(const Message &msg) const override;
        bool overridesCpuDemand() const override { return true; }

        // Class annotation (docs/NoC_Architecture_Spec.md s14.3) with the TileLink channel of every
        // message (plan s5.2), and the admission prediction (plan s5.2 point 1, TLC_Tables s7.1).
        bool consumedBy(const std::string &event, MsgClass &out) const override;
        bool emittedBy(const std::string &action, std::vector<MsgClass> &out) const override;
        bool hasClassAnnotation() const override { return true; }
        bool predictEmissions(const Message &msg, GenericCacheLine &line, std::vector<MsgClass> &out) override;

        const tl::coh::L1Policy &policy() const { return *m_policy; }

    protected:
        std::unique_ptr<tl::coh::L1Policy> m_policy;
        int m_stall_id = -1;
        unsigned m_lg_block = 6;

        uint64_t blockOf(uint64_t addr) const { return addr & ~uint64_t(m_data_handler->getBlockSize() - 1); }
        int  stateOf(uint64_t addr) const;                   // the line's state; I when absent
        // TLC_Tables s2.2: the event a message decodes to; fatal on a decode fault
        int  decode(const Message &msg) const;
        bool isStall(int state, int event) const { return m_stall_id >= 0 && m_policy->table().hasAction(state, event, m_stall_id); }
        // the Octopus message of a TileLink emission: header from L1Policy::build, kind (TLC_Tables s8)
        Message emission(int action, const Message &trigger, uint64_t msg_id) const;
        // the self-raised Replacement of a victim a Probe has taken since (TLC_L1.cpp): a no-op
        bool staleReplacement(const Message &msg) const;
        [[noreturn]] void fault(const std::string &why, const Message &msg, int state, int event) const;
    };
}

#endif /* _TLC_L1_H */
