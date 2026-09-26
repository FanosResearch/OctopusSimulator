/*
 * Copyright (c) 2025 FanosLab
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gem5/octopus.hh"

#include "base/compiler.hh"
#include "base/random.hh"
#include "debug/Octopus.hh"
#include "sim/core.hh"
#include "sim/system.hh"

#define external_cache

namespace gem5
{

//int Octopus::connection_id = 0;
octopus::CacheSim* Octopus::cache_sim = NULL;
bool Octopus::ext_cache_started = false;
uint64_t Octopus::num_pending_req = 0;
std::vector<Octopus*> Octopus::instances;

Octopus::Octopus(const OctopusParams &params) :
    ClockedObject(params),
    system(params.system),
    tickEvent([this]{ tick(); }, name()),
    blockSize(params.system->cacheLineSize()),
    octopusCycleNs(params.octopus_cycle_ns),
    memPort(params.name + ".mem_side", this)
{
    // Since the CPU side ports are a vector of ports, create an instance of
    // the CPUSidePort for each connection. This member of params is
    // automatically created depending on the name of the vector port and
    // holds the number of connections to this port name
    if (Octopus::cache_sim == NULL) {
        inform("Octopus: system %s, config %s, %d override(s)",
               params.system_name, params.config_name,
               (int)params.cl_params.size());
        Octopus::cache_sim = new octopus::CacheSim(params.system_name,
                                                   params.cl_params,
                                                   false, params.config_name);
    }

    // The bridge and libOctopus.so are built separately. A stale one of the
    // two would otherwise disagree about the port interface without any error.
    fatal_if(octopus::ExternalCPU::portVersion() != octopus::kOctopusPortVersion,
             "Octopus port version mismatch: bridge built against %d, loaded "
             "libOctopus.so provides %d. Rebuild both.\n",
             octopus::kOctopusPortVersion, octopus::ExternalCPU::portVersion());

    auto *ext_cpus = octopus::ExternalCPU::getExtCPUs();
    fatal_if(ext_cpus->find(params.cache_id) == ext_cpus->end(),
             "%s: Octopus has no ExternalCPU with id %d. The system preset "
             "(%s) must set cpu_type=ExternalCPU and have num_cores > %d.\n",
             name(), params.cache_id, params.config_name, params.cache_id);
    extCpu = ext_cpus->at(params.cache_id);

    extCpu->registerCPUCallback(
        new octopus::CPUCallback<Octopus, uint64_t, uint64_t, uint64_t,
                                 octopus::RequestType, uint8_t*>(
            this, &Octopus::cacheSimCallback));
    extCpu->registerCommitCallback(
        new octopus::CPUCallback<Octopus, uint64_t, uint64_t>(
            this, &Octopus::onCommit));
    extCpu->registerInvalidateCallback(
        new octopus::CPUCallback<Octopus, uint64_t>(
            this, &Octopus::onInvalidate));

    Octopus::instances.push_back(this);

    // Without a credit bound the link FIFOs toward the L1 could overflow,
    // which the library treats as fatal.
    warn_if(extCpu->outstandingLimit() < 0,
            "%s: the Octopus L1 has an unbounded processing queue; the "
            "bridge cannot bound outstanding requests\n", name());

    registerExitCallback([this]() {
        if (snoopsSent)
            cprintf("%s: %llu invalidation snoops sent to the CPU\n",
                    name(), snoopsSent);
        if (heldBackOps)
            cprintf("%s: %llu data operations were held back behind an older "
                    "overlapping write from the same core\n",
                    name(), heldBackOps);
        if (portBlocks)
            cprintf("%s: %llu CPU-port refusals while the Octopus L1 was "
                    "blocked\n", name(), portBlocks);
    });

    for (int i = 0; i < params.port_cpu_side_connection_count; ++i) {
        cpuPorts.emplace_back(name() + csprintf(".cpu_side[%d]", i),"CpuSidePort", params.cache_id, i, this);
    }
    DPRINTF(Octopus, "Connect to cache id %d\n", params.cache_id);
}

Addr
Octopus::getAddr(PacketPtr pkt) const
{
    // Check if request object exists and has physical address
    if (pkt->req && pkt->req->hasPaddr()) {
        return pkt->req->getPaddr();
    } else {
        // Fall back to packet's address (handles both vaddr and missing req cases)
        return pkt->getAddr();
    }
}

uint64_t
Octopus::submit(PacketPtr pkt, octopus::RequestType type, int connection_id,
                int port_id)
{
    const uint64_t msg_id =
        extCpu->addRequest(
            getAddr(pkt), type,
            NULL /*Do not attenpt to get ptr to avoid masked write assertion*/,
            pkt->getSize());

    const bool inserted = pending_requests.emplace(
        msg_id, PendingTxn{pkt, connection_id, port_id, false, nextSeq++,
                           false, false}).second;
    panic_if(!inserted, "%s: msg_id %llu issued twice\n", name(), msg_id);
    return msg_id;
}

bool
Octopus::blockedByOlderWrite(const PendingTxn &txn) const
{
    const Addr lo = getAddr(txn.pkt);
    const Addr hi = lo + txn.pkt->getSize();
    for (const auto &entry : pending_requests) {
        const PendingTxn &o = entry.second;
        if (o.seq >= txn.seq || o.dataDone || !o.pkt->isWrite())
            continue;
        const Addr olo = getAddr(o.pkt);
        if (olo < hi && lo < olo + o.pkt->getSize())
            return true;
    }
    return false;
}

void
Octopus::performData(uint64_t msg_id)
{
    PendingTxn &txn = pending_requests.at(msg_id);
    const bool is_write = txn.pkt->isWrite();

    // One step for every kind of access: AbstractMemory::access() reads,
    // writes, registers the reservation of a load-exclusive, validates a
    // store-exclusive and sets its result, and runs the functor or the
    // compare of a swap/atomic. A write-type request only commits once this
    // L1 holds the line in M, so the read-modify-write is atomic in the
    // coherence model as well as in the data.
    memPort.sendAtomic(txn.pkt);
    assert(txn.pkt->isResponse());
    txn.dataDone = true;

    if (txn.completionArrived)
        respond(msg_id);            // erases the entry; txn is dead after this
    if (is_write)
        releaseHeldBack();
}

void
Octopus::releaseHeldBack()
{
    // Oldest first. performData() can erase entries and recurse, so work
    // from ids and look each one up again.
    std::vector<std::pair<uint64_t, uint64_t>> held;   // seq, msg_id
    for (const auto &entry : pending_requests) {
        if (entry.second.committed && !entry.second.dataDone)
            held.emplace_back(entry.second.seq, entry.first);
    }
    std::sort(held.begin(), held.end());
    for (const auto &h : held) {
        auto itr = pending_requests.find(h.second);
        if (itr == pending_requests.end() || itr->second.dataDone)
            continue;
        if (!blockedByOlderWrite(itr->second))
            performData(h.second);
    }
}

void
Octopus::respond(uint64_t msg_id)
{
    auto itr = pending_requests.find(msg_id);
    const PendingTxn txn = itr->second;
    pending_requests.erase(itr);
    num_pending_req--;
    assert(txn.pkt->isResponse());
    cpuPorts[txn.port_id].sendPacket(txn.pkt);

    // Completing a request may have unblocked the L1.
    retryBlockedPorts();
}

void
Octopus::onCommit(uint64_t msg_id, uint64_t address)
{
    auto itr = pending_requests.find(msg_id);
    panic_if(itr == pending_requests.end(),
             "%s: commit for unknown msg_id %llu (addr %#x)\n",
             name(), msg_id, address);

    PendingTxn &txn = itr->second;
    panic_if(txn.committed, "%s: msg_id %llu committed twice\n", name(), msg_id);
    panic_if(getAddr(txn.pkt) != address,
             "%s: msg_id %llu committed with addr %#x, submitted with %#x\n",
             name(), msg_id, address, getAddr(txn.pkt));
    txn.committed = true;

    if (blockedByOlderWrite(txn)) {
        heldBackOps++;
        return;                     // performed by releaseHeldBack()
    }
    performData(msg_id);
}

void Octopus::cacheSimCallback(uint64_t msg_id, uint64_t address, uint64_t cycle, octopus::RequestType type, uint8_t* data)
{
    auto itr = pending_requests.find(msg_id);
    panic_if(itr == pending_requests.end(),
             "%s: completion for unknown msg_id %llu (addr %#x type %d). "
             "pending=%d\n",
             name(), msg_id, address, (int)type, (int)pending_requests.size());

    PendingTxn &txn = itr->second;
    panic_if(!txn.committed,
             "%s: msg_id %llu (addr %#x) completed without a commit: the "
             "engine has a CPU completion site with no commit hook\n",
             name(), msg_id, address);

    if (!txn.dataDone) {
        // Held back behind an older write; performData() will respond.
        txn.completionArrived = true;
        return;
    }
    respond(msg_id);
}

Port &
Octopus::getPort(const std::string &if_name, PortID idx)
{
    // This is the name from the Python SimObject declaration in Octopus.py
    if (if_name == "mem_side") {
        panic_if(idx != InvalidPortID,
                 "Mem side of simple cache not a vector port");
        return memPort;
    } else if (if_name == "cpu_side" && idx < cpuPorts.size()) {
        // We should have already created all of the ports in the constructor
        return cpuPorts[idx];
    } else {
        // pass it along to our super class
        return ClockedObject::getPort(if_name, idx);
    }
}

void
Octopus::CPUSidePort::sendPacket(PacketPtr pkt)
{
    DPRINTF(Octopus, "Sending %s to CPU\n", pkt->print());
    //pkt->makeResponse();
    schedTimingResp(pkt, curTick());
}

AddrRangeList
Octopus::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
Octopus::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    owner->handleFunctional(pkt, connection_id);
}

Tick
Octopus::CPUSidePort::recvAtomic(PacketPtr pkt)
{
    return owner->handleAtomic(pkt, connection_id, id);
    // 1 ns is just an arbitrary value at this point

}

bool
Octopus::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(Octopus, "Got request %s size=%d %d\n", pkt->print(),pkt->getSize(), curTick());
    return owner->accessTiming(pkt, connection_id, id);
}

void
Octopus::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

bool
Octopus::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the cache.
    return owner->handleResponse(pkt);
}

void
Octopus::MemSidePort::recvReqRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void
Octopus::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

bool
Octopus::handleResponse(PacketPtr pkt)
{
    // forward packet to octopus only if this request if send from octopus mem ctrl
    bool isMemRead = pkt->isRead();

    // if (isMemRead)
    //     octopus::ExternalMem::getExtMem()->read_callback(pkt->req->getPaddr(), curTick());
    // else
    //     octopus::ExternalMem::getExtMem()->write_callback(pkt->req->getPaddr(), curTick());
    // delete pkt;

    return true;
}

void
Octopus::handleFunctional(PacketPtr pkt, int connection_id)
{
    // Memory is the only copy of the data, but a write that has been accepted
    // and not yet committed exists only in its packet. Let a functional read
    // see it, as a classic cache does with its queued packets.
    for (auto &entry : pending_requests) {
        const PendingTxn &txn = entry.second;
        if (!txn.committed && txn.pkt->isWrite() &&
            pkt->trySatisfyFunctional(txn.pkt))
            return;
    }
    memPort.sendFunctional(pkt);
}

Tick
Octopus::handleAtomic(PacketPtr pkt, int connection_id, int port_id)
{
    return memPort.sendAtomic(pkt);
    // assert(pkt->isResponse());
}

void
Octopus::onInvalidate(uint64_t address)
{
    deliverInvalidate(Addr(address) & ~(Addr(blockSize) - 1));
}

void
Octopus::deliverInvalidate(Addr blk_addr)
{
    for (auto &port : cpuPorts) {
        // Only a data port declares itself a snooper (O3's DcachePort does);
        // a fetch port does not and would panic on a snoop.
        if (!port.isSnooping())
            continue;

        RequestPtr req = std::make_shared<Request>(
            blk_addr, blockSize, 0, Request::funcRequestorId);
        Packet snoop_pkt(req, MemCmd::InvalidateReq);
        snoop_pkt.setExpressSnoop();

        // Synchronous, as Cache::handleSnoop() forwards upward. It passes the
        // isInvalidate() gate in LSQ::recvTimingSnoopReq() and lands in
        // LSQUnit::checkSnoop(): matching monitors are cleared under
        // noSquashFromTC and conflicting loads are marked for re-execution.
        port.sendTimingSnoopReq(&snoop_pkt);
        assert(!snoop_pkt.cacheResponding());   // a CPU never sources data
        snoopsSent++;
    }
}

void
Octopus::bypass(PacketPtr pkt, int port_id)
{
    memPort.sendAtomic(pkt);
    if (pkt->isResponse())
        cpuPorts[port_id].sendPacket(pkt);
}

bool
Octopus::accessTiming(PacketPtr pkt, int connection_id, int port_id)
{
    // Invalidatation and clean not implemented
    if (pkt->isInvalidate() || pkt->isClean()) {
        pkt->makeResponse();
        cpuPorts[port_id].sendPacket(pkt);
        return true;
    }

    // Devices and uncacheable requests are not cache lines. A classic cache
    // forwards them unchanged; do the same and keep them out of Octopus.
    const bool cacheable = (pkt->isRead() || pkt->isWrite()) &&
                           !pkt->req->isUncacheable() &&
                           system->isMemAddr(getAddr(pkt));
    if (!cacheable) {
        bypass(pkt, port_id);
        return true;
    }

    // Admission comes before every side effect. When recvTimingReq() returns
    // false the sender keeps the packet and retries it, so a rejected packet
    // must not have touched memory or been turned into a response.
    //
    // The decision mirrors a classic cache's blocked port: refuse while the
    // Octopus L1 could not admit one more demand request (MSHRs, demand
    // queue or write-back buffer full, counting what is already in transit
    // toward it). The retry goes out from tick() or respond() once the L1
    // has room again.
    if (portBlocked()) {
        cpuPorts[port_id].needRetry = true;
        portBlocks++;
        return false;
    }

    // Nothing touches memory here. The packet waits, still a request, until
    // Octopus commits it (onCommit) and later completes it. Anything that
    // writes -- store, store-exclusive, swap, atomic -- is one write-type
    // transaction, so it commits only with the line held in M.
    submit(pkt,
           pkt->isWrite() ? octopus::RequestType::WRITE : octopus::RequestType::READ,
           connection_id, port_id);
    num_pending_req++;
    return true;
}

bool
Octopus::portBlocked() const
{
    // Every accepted request holds one of the core's credits until its
    // response is sent (squashed ones included), so the pending table is the
    // outstanding count the L1 bounds.
    return extCpu->isBlocked((int)pending_requests.size());
}

void
Octopus::retryBlockedPorts()
{
    if (portBlocked())
        return;
    // One retry per unblock, as BaseCache::clearBlocked() does; the sender
    // re-issues one packet and is refused again if the L1 fills up.
    for (auto &port : cpuPorts) {
        if (port.needsRetry()) {
            port.sendRetryReq();
            break;
        }
    }
}

bool
Octopus::anyPortNeedsRetry()
{
    for (Octopus *bridge : instances)
        for (auto &port : bridge->cpuPorts)
            if (port.needsRetry())
                return true;
    return false;
}

AddrRangeList
Octopus::getAddrRanges() const
{
    DPRINTF(Octopus, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return memPort.getAddrRanges();
}

void
Octopus::sendRangeChange() const
{
    for (auto& port : cpuPorts) {
        port.sendRangeChange();
    }
}

void Octopus::tick()
{
    // Keep Octopus running while a port waits on it, even with nothing
    // pending: the L1 may be draining its write-back buffer.
    if (num_pending_req || anyPortNeedsRetry()) {
        // Advance Octopus by one of its cache cycles per gem5 cycle.
        //
        // This used to be a fixed two step() calls. That is only correct when
        // one step covers half a cache cycle, which holds when the finest
        // registered Octopus clock is the Bus half-period. ClockManager::
        // clkStep() advances to the next scheduled event, so any component
        // registered on a finer period shrinks every step and silently dilates
        // the whole model against gem5's clock. Pace against Octopus's own
        // clock instead, which stays correct for any configuration.
        const uint64_t target = cache_sim->now() + octopusCycleNs;
        unsigned guard = 0;
        while (cache_sim->now() < target) {
            cache_sim->step();
            panic_if(++guard > 1024,
                     "%s: Octopus advanced < %u ns in 1024 steps (now=%llu). "
                     "A registered clock period is far finer than expected.\n",
                     name(), octopusCycleNs, cache_sim->now());
        }
        // The L1s may have freed MSHRs, queue slots or write-back entries.
        for (Octopus *bridge : instances)
            bridge->retryBlockedPorts();
    }
    schedule(tickEvent, clockEdge(Cycles(1)));
}

void Octopus::setOctLoggerEn(bool enable)
{
    warn_once("%s: setOctLoggerEn(%d) is a no-op with this Octopus tree\n",
              name(), enable);
}

void Octopus::startup()
{
    // kick off the clock ticks
    if(!Octopus::ext_cache_started) {
        // Report the mapping so a misconfigured Octopus clock shows up in the
        // log rather than as unexplained latency.
        const uint64_t gran = cache_sim->stepGranularity();
        const uint64_t steps = gran ? octopusCycleNs / gran : 0;
        inform("%s: 1 gem5 cycle (%llu ps) = %u Octopus ns = %llu step(s)\n",
               name(), clockPeriod(), octopusCycleNs, steps);

        // Every period must divide the per-cycle advance, or a component lands
        // mid-advance and does not run at the rate its XML entry states.
        warn_if(gran == 0 || octopusCycleNs % gran != 0,
                "%s: Octopus step granularity %llu ns does not divide the "
                "per-cycle advance of %u ns; component rates will not match "
                "the configuration.\n", name(), gran, octopusCycleNs);

        // A well-formed config steps twice per cycle: once for the Bus
        // half-period, once for the cache period. Many more means some
        // component is registered far finer -- typically a clock period left at
        // its 1 ns default in the CSV. Everything still runs at its
        // stated period in Octopus ns, but the model as a whole crawls
        // relative to gem5, which reads as uniformly huge memory latency.
        warn_if(steps > 8,
                "%s: %llu Octopus steps per gem5 cycle (expected 2). Some "
                "component is registered on a %llu ns clock. Check the "
                "ClockManager period report above for a stray fine period.\n",
                name(), steps, gran);

        schedule(tickEvent, clockEdge(Cycles(1)));
    }
    Octopus::ext_cache_started = true;
}

} // namespace gem5

