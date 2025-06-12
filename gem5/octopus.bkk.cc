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
#include "sim/system.hh"

#define external_cache

namespace gem5
{

//int Octopus::connection_id = 0;
ns3::CacheSim* Octopus::cache_sim = NULL;
bool Octopus::ext_cache_started = false;
map<uint64_t, pair<uint64_t, vector<Octopus::LockEntry>*>> Octopus::atomicLockMap;
std::list<Octopus::LockedAddr> Octopus::lockedAddrList;
uint64_t Octopus::num_pending_req = 0;

Octopus::Octopus(const OctopusParams &params) :
    ClockedObject(params),
    blockSize(params.system->cacheLineSize()),
    reqFIFOSize(params.req_fifo_size),
    outstandingReqs(0),
    memPort(params.name + ".mem_side", this),
    system(params.system),
    tickEvent([this]{ tick(); }, name())
{
    // Since the CPU side ports are a vector of ports, create an instance of
    // the CPUSidePort for each connection. This member of params is
    // automatically created depending on the name of the vector port and
    // holds the number of connections to this port name
    cout << "Octopus " << params.config_file_path.c_str() <<" "<<params.output_logs_path.c_str() << endl;
    if(Octopus::cache_sim == NULL)
        Octopus::cache_sim = new ns3::CacheSim(params.config_file_path.c_str(),
                                      params.output_logs_path.c_str());
    // cache_sim->run();

    ns3::CPUCallback<Octopus, uint64_t, uint64_t, ns3::RequestType, uint8_t*>* cache_sim_callback =
        new ns3::CPUCallback<Octopus, uint64_t, uint64_t, ns3::RequestType, uint8_t*>(this, &Octopus::cacheSimCallback);
    ns3::ExternalCPU::getExtCPUs()->at(params.cache_id)->registerCPUCallback(cache_sim_callback);

    ns3::MemCallback<Octopus, uint64_t, uint64_t, ns3::RequestType, uint8_t*>* cache_sim_mem_callback =
        new ns3::MemCallback<Octopus, uint64_t, uint64_t, ns3::RequestType, uint8_t*>(this, &Octopus::cacheSimMemCallback);
    // ns3::ExternalMem::getExtMem()->registerMemCallback(Octopus::connection_id, cache_sim_mem_callback);

    for (int i = 0; i < params.port_cpu_side_connection_count; ++i) {
        cpuPorts.emplace_back(name() + csprintf(".cpu_side[%d]", i),"CpuSidePort", params.cache_id, i, this);
    }
    DPRINTF(Octopus, "Connect to cache id %d\n", params.cache_id);

    //Octopus::connection_id++;
}

void Octopus::cacheSimCallback(uint64_t address, uint64_t cycle, ns3::RequestType type, uint8_t* data)
{
    auto itr = std::find_if(pending_requests.begin(), pending_requests.end(),
                            [&](pair<PacketPtr, int> entry) -> bool{
                                return entry.first->getAddr() == address;
                            });
    assert(itr != pending_requests.end()); // we should always find a coresponding pkt
    PacketPtr pkt = itr->first;
    int port_id = itr->second;

    // forward the packet to the memory side
    // If the pkt is write, we have to clear all the LLSC record in checkLockedAddrList.
    // If a SC is failed again, do not write the memory. See details in checkLockedAddrList.
    if (!pkt->isWrite() || writeOK(pkt, true/*doErase*/))
        memPort.sendFunctional(pkt);
    else 
        pkt->makeResponse();

    // Check if this is a swap operation by looking up address in swapDataMap
    bool swapCompleted = false;
    bool isSwap = false;
    auto swapIt = swapDataMap.find(pkt->req->getPaddr());
    if (swapIt != swapDataMap.end()) {
        swapCompleted = handleSwap(pkt);
        isSwap = true;
    }


    if(pkt->cmd == MemCmd::LockedRMWWriteResp || (swapCompleted && isSwap))
    {
        DPRINTF(Octopus, "Release lock by %s\n", pkt->print());
        auto vect = atomicLockMap[pkt->req->getPaddr() & ~(blockSize-1)].second;

        auto it = atomicLockMap.find(pkt->req->getPaddr() & ~(blockSize-1));

        atomicLockMap.erase(it);
        for(auto v : *vect)
            v.func_ptr(v.pkt, v.connection_id, v.port_id);

        delete vect;
    }

    if (!isSwap || swapCompleted){
        pending_requests.erase(itr);
        num_pending_req --;
        assert(pkt->isResponse());
        this->sendResponse(pkt, port_id);
    }
}

void Octopus::cacheSimMemCallback(uint64_t address, uint64_t cycle, ns3::RequestType type, uint8_t* data)
{
    if (type == ns3::RequestType::SETUP_READ || type == ns3::RequestType::SETUP_WRITE)
    {
        assert("receive functional request\n");
    }

    //bool isMemRead = data == NULL;
    bool isMemRead = type == ns3::RequestType::READ;
    MemCmd cmd;
    PacketPtr new_pkt;
    if (isMemRead)
        cmd = MemCmd::ReadReq;
    else
        cmd = MemCmd::WriteReq;

    auto itr = std::find_if(pending_requests.begin(), pending_requests.end(),
                            [&](pair<PacketPtr, int> entry) -> bool{
                                return entry.first->getAddr() == address;
                            });
    
    // Never write the actuall data at this stage in case this is a failed LLSC.
    // If this is a write, read the data first and write back what has been read.
    // So the actual data is not changed. We just use the timing of the memory component.
    if (itr == pending_requests.end())
    {
        //panic("Packet not found\n");
        //it is possibly a writeback dirty, since octopus does not handle accurate data
        //it will be necessary to get the actuate data first before sending the write request
        RequestPtr req = std::make_shared<Request>(
            address, blockSize, 0, 0);

        //funtional read to get the correct write data
        if (!isMemRead)
        {
            new_pkt = new Packet(req, MemCmd::ReadReq, blockSize);
            // Allocate memory for the data
            uint8_t *data = new uint8_t[blockSize];
            new_pkt->dataDynamic(data);

            // Send the packet
            memPort.sendFunctional(new_pkt);

            //change to write
            new_pkt->cmd = cmd;
        }
        //no need funtional read for a read request
        else
        {
            new_pkt = new Packet(req, cmd, blockSize);
        }

        // cout << "cacheSimMemCallback not found " <<std::hex << new_pkt->print()<<" " <<address <<" "<<*data << endl;
    }
    else
    {
        PacketPtr pkt = itr->first;
        int port_id = itr->second;

        // Forward to the memory side.
        Addr addr = pkt->getAddr();

        // Create a new package
        new_pkt = new Packet(pkt->req, cmd);
        new_pkt->allocate();
        if (!isMemRead){
            new_pkt->cmd = MemCmd::ReadReq;
            memPort.sendFunctional(new_pkt);

            //change to write
            new_pkt->cmd = cmd;
        }
    }
    // DPRINTF(Octopus, "forwarding packet\n");
    memPort.sendPacket(new_pkt);
    // cout << "cacheSimMemCallback send " <<std::hex << new_pkt->print()<< " "<< this<< endl;
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
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    DPRINTF(Octopus, "Sending %s to CPU\n", pkt->print());
    schedTimingResp(pkt, curTick());
}

AddrRangeList
Octopus::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
Octopus::CPUSidePort::trySendRetry()
{
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(Octopus, "Sending retry req.\n");
        sendRetryReq();
    }
}

void
Octopus::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    owner->handleFunctional(pkt, connection_id);
}

Tick
Octopus::CPUSidePort::recvAtomic(PacketPtr pkt)
{
    owner->handleAtomic(pkt, connection_id, id);

    // 1 ns is just an arbitrary value at this point
    return 1000;
}

bool
Octopus::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(Octopus, "Got request %s size=%d %d\n", pkt->print(),pkt->getSize(), curTick());

    if (blockedPacket || needRetry) {
        // The cache may not be able to send a reply if this is blocked
        DPRINTF(Octopus, "Request blocked\n");
        needRetry = true;
        return false;
    }
    // Just forward to the cache.
    owner->accessTiming(pkt, connection_id, id);
    return true;
}

// void
// Octopus::CPUSidePort::recvRespRetry()
// {
//     DPRINTF(Octopus, "Retrying response");
//     // We should have a blocked packet if this function is called.
//     assert(blockedPacket != nullptr);

//     // Grab the blocked packet.
//     PacketPtr pkt = blockedPacket;
//     blockedPacket = nullptr;

//     DPRINTF(Octopus, " pkt %s\n", pkt->print());
//     // Try to resend it. It's possible that it fails again.
//     sendPacket(pkt);

//     // We may now be able to accept new packets
//     trySendRetry();
// }

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
    // print2file(pkt);
    // forward packet to octopus only if this request if send from octopus mem ctrl
    bool isMemRead = pkt->isRead();

    // // cout << "handleResponse"<<std::hex <<pkt <<" " <<pkt->req->getPaddr() << " "<< isMemRead <<" "<< pkt->needsResponse()<<" " <<this<<  endl;
    // if (isMemRead)
    //     ns3::ExternalMem::getExtMem()->read_callback(pkt->req->getPaddr(), curTick());
    // else
    //     ns3::ExternalMem::getExtMem()->write_callback(pkt->req->getPaddr(), curTick());
    // delete pkt;

    return true;
}

void Octopus::sendResponse(PacketPtr pkt, int port_id)
{
    // Simply forward to the memory port
    cpuPorts[port_id].sendPacket(pkt);

    // For each of the cpu ports, if it needs to send a retry, it should do it
    // now since this memory object may be unblocked now.
    // for (auto& port : cpuPorts) {
    //     port.trySendRetry();
    // }
}

void
Octopus::handleFunctional(PacketPtr pkt, int connection_id)
{
    memPort.sendFunctional(pkt);
}

void
Octopus::handleAtomic(PacketPtr pkt, int connection_id, int port_id)
{
    memPort.sendAtomic(pkt);
    assert(pkt->isResponse());
}

void
Octopus::accessTiming(PacketPtr pkt, int connection_id, int port_id)
{
    // Handle Read-Modify-Write (RMW) or swap operations
    // - If another core has locked this cache line, queue this request
    // - If this is a new RMW/Load-Locked request, acquire the lock for this core
    if(atomicLockMap.find(pkt->req->getPaddr() & ~(blockSize-1)) != atomicLockMap.end() &&
        atomicLockMap[pkt->req->getPaddr() & ~(blockSize-1)].first != connection_id)
    {
        LockEntry entry;
        entry.pkt = pkt; entry.connection_id = connection_id; entry.port_id = port_id;
        entry.func_ptr = [&](PacketPtr pkt, int connection_id, int port_id)->void
                            {
                                // cout << "callback on line @ = " << std::hex << (pkt->req->getPaddr() & ~(blockSize-1))
                                //     << " from core: " << connection_id << endl;

                                this->accessTiming(pkt, connection_id, port_id);
                            };

        atomicLockMap[pkt->req->getPaddr() & ~(blockSize-1)].second->push_back(entry);
        DPRINTF(Octopus, "Pkt %s waiting line @ = 0x%x from core %d\n",pkt->print(), pkt->req->getPaddr() & ~(blockSize-1), connection_id);
        return;
    }

    if(pkt->cmd == MemCmd::LockedRMWReadReq)
    {
        atomicLockMap[pkt->req->getPaddr() & ~(blockSize-1)] =
                std::make_pair(connection_id, new vector<LockEntry>);
        DPRINTF(Octopus, "Lock line @ = 0x%x from core %d by pkt %s\n",pkt->req->getPaddr() & ~(blockSize-1), connection_id, pkt->print());
    }
    if (pkt->cmd == MemCmd::SwapReq)
    {
        writeOK(pkt, false); // used to clear load lockes since swap is also a write
        Addr addr = pkt->req->getPaddr();
        // lock the line
        atomicLockMap[addr & ~(blockSize-1)] =
                std::make_pair(connection_id, new vector<LockEntry>);

        // Save original data from packet and connection_id
        unsigned size = pkt->getSize();
        uint8_t *oldData = new uint8_t[size];
        std::memcpy(oldData, pkt->getConstPtr<uint8_t>(), size);
        swapDataMap[pkt->req->getPaddr()] = std::make_pair(oldData, connection_id);
        
        DPRINTF(Octopus, "Line %#x locked for swap operation\n", addr);

        // Convert to read request
        pkt->cmd = MemCmd::ReadReq;
        
        // Process as read
        ns3::ExternalCPU::getExtCPUs()->at(connection_id)->addRequest(pkt->req->getPaddr(), ns3::RequestType::READ, NULL, pkt->getSize());
        pending_requests.push_back(std::make_pair(pkt, port_id));
        num_pending_req++;
    }
    else if (pkt->isWrite()) {
        if (writeOK(pkt, false/*doErase*/, connection_id, port_id)) {
            ns3::ExternalCPU::getExtCPUs()->at(connection_id)->addRequest(pkt->req->getPaddr(), ns3::RequestType::WRITE, NULL /*Do not attenpt to get ptr to avoid masked write assertion*/, pkt->getSize());
            pending_requests.push_back(std::make_pair(pkt, port_id));
            num_pending_req++;
        }
        else {
            // fail the sc or packet was queued as a delayed write
            if (pkt->isLLSC()) {
                pkt->makeResponse();
                this->sendResponse(pkt, port_id);
            }
            // If it's a normal write that was delayed, we don't need to do anything
            // because it's been queued as a delayed write
        }
    }
    else if (pkt->isRead()) {
        if (pkt->isLLSC()) {
            trackLoadLocked(pkt);
        }
        ns3::ExternalCPU::getExtCPUs()->at(connection_id)->addRequest(pkt->req->getPaddr(), ns3::RequestType::READ, NULL, pkt->getSize());
        pending_requests.push_back(std::make_pair(pkt, port_id));
        num_pending_req++;
    }
    else {
        // for O3CPU which use LSQ, cannot reponse in the same stack frame. therefore, we need to schedule the response in queue
        if (pkt->isInvalidate() || pkt->isClean()) {
            pkt->makeResponse();
            this->sendResponse(pkt, port_id);
            return;
        } 
        panic("Unknown packet type!  %s\n", pkt->print().c_str());
    }
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
    if (num_pending_req) {
        cache_sim->step();
        cache_sim->step();
        }
    schedule(tickEvent, clockEdge(Cycles(1)));
}

void Octopus::setOctLoggerEn(bool enable)
{
    ns3::ExternalCPU::getExtCPUs()->at(0)->setLoggerEnable(enable);
}
void Octopus::startup()
{
    // kick off the clock ticks
    if(!Octopus::ext_cache_started)
        schedule(tickEvent, clockEdge(Cycles(1)));
    Octopus::ext_cache_started = true;
}

bool  
Octopus::handleSwap(PacketPtr pkt)
{   
    bool completed = false;
    auto dataIt = swapDataMap.find(pkt->req->getPaddr());
    if (dataIt == swapDataMap.end()) {
        // No data found for this address
        gem5_assert(false, "No data found for this address");
    }
    uint8_t *dataBuffer = dataIt->second.first;  // Original data from packet
    int conn_id = dataIt->second.second;  // Connection ID for this swap

    if (pkt->isWrite()){
        completed = true;
        pkt->setData(dataBuffer);
    }
    else if (pkt->isRead()){
        // Find original data buffer for this address
        if (pkt->isAtomicOp()) {
            // Execute AMO operation
            // Copy packet data to buffer before executing atomic operation
            std::memcpy(dataBuffer, pkt->getPtr<uint8_t>(), pkt->getSize());
            (*(pkt->getAtomicOp()))(pkt->getPtr<uint8_t>());
        } else if (pkt->req->isCondSwap()) {
            uint64_t condition = pkt->req->getExtraData();
            bool success = false;

            if (pkt->getSize() == sizeof(uint64_t)) {
                success = !std::memcmp(&condition, pkt->getPtr<uint8_t>(), sizeof(uint64_t));
            } else if (pkt->getSize() == sizeof(uint32_t)) {
                success = !std::memcmp(&condition, pkt->getPtr<uint8_t>(), sizeof(uint32_t));
            }

            if (!success) {
                completed = true;
            }
        }
    }

    if (completed) {
        delete[] dataIt->second.first; // Delete data buffer
        swapDataMap.erase(dataIt);
        // Convert packet back to swap operation
        pkt->cmd = MemCmd::SwapResp;
        return true;
    }
    else {
        // Swap not completed, do write operation
        pkt->cmd = MemCmd::WriteReq;
        if (!pkt->isAtomicOp()){
            // Swap data between packet and buffer
            uint8_t* pktData = pkt->getPtr<uint8_t>();
            std::swap_ranges(pktData, pktData + pkt->getSize(), dataBuffer);
        }
        ns3::ExternalCPU::getExtCPUs()->at(conn_id)->addRequest(pkt->req->getPaddr(), ns3::RequestType::WRITE, NULL, pkt->getSize());
        
        return false;
    }
}

// Add load-locked to tracking list.  Should only be called if the
// operation is a load and the LLSC flag is set.
void
Octopus::trackLoadLocked(PacketPtr pkt)
{
    const RequestPtr &req = pkt->req;
    Addr paddr = LockedAddr::mask(req->getPaddr());
    bool foundPendingWrite = false;

    // first we check if we already have a locked addr for this
    // xc.  Since each xc only gets one, we just update the
    // existing record with the new address.
    std::list<LockedAddr>::iterator i;

    // If there is a pending write to the same address
    // We still need to track this LL, but mark the write flag as true
    // so that when the write done it can issue WFE to this core
    for (i = lockedAddrList.begin(); i != lockedAddrList.end(); ++i) {
        if (i->addr == paddr && i->writePending){
            foundPendingWrite = true;
            break;
        }
    }

    for (i = lockedAddrList.begin(); i != lockedAddrList.end(); ++i) {
        // If we find delayed writes for this address, we will create a new record instead of updating this one. 
        if (i->matchesContext(req) && i->delayedWrites.empty()) {
            DPRINTF(Octopus, "Modifying lock record: context %d addr %#x\n",
                    req->contextId(), paddr);
            i->addr = paddr;
            i->writePending = foundPendingWrite; // reinitialize the record
            return;
        }
    }

    // no record for this xc: need to allocate a new one
    DPRINTF(Octopus, "Adding lock record: context %d addr %#x foundPendingWrite %d\n",
            req->contextId(), paddr, foundPendingWrite);
    lockedAddrList.push_front(LockedAddr(req, foundPendingWrite));
}


// Called on *writes* only... both regular stores and
// store-conditional operations.  Check for conventional stores which
// conflict with locked addresses, and for success/failure of store
// conditionals.
//
// This will be called twice:
// 1. Before send to octopus/cachesim (to check if a store can process)
// 2. Resp by octopus/cachesim (to check if a store can process + erase lock record)
//
// Notes about the 2nd pass: 
// A normal write is delayed if there is a pending SC to the same address.
// This is to prevent this scenario:
// Cycle 0
//   SC req addrA -> success before it was send to octopus/cachesim
// Cycle 1
//   write req addrA -> direct write will process no matter what
// Cycle 2
//   write resp addrA -> octopus chooses to resp this first. Can happend if we use diff types of interconnect or arbiters
//                    -> this will write at actual mem
//                    -> this will also clear the context of the SC req arrived at cycle 0
// Cycle 3
//   SC resp addrA -> we have to fail this here, since a write req has been writen before it. 
//                 -> however since at octopus side this already has a status updated, we want it to always susscess after reaching oct
bool
Octopus::checkLockedAddrList(PacketPtr pkt, bool doErase, int connection_id, int port_id)
{
    const RequestPtr &req = pkt->req;
    Addr paddr = LockedAddr::mask(req->getPaddr());
    bool isLLSC = pkt->isLLSC();

    // Initialize return value.  Non-conditional stores always
    // succeed.  Assume conditional stores will fail until proven
    // otherwise.
    bool allowStore = !isLLSC;

    std::list<LockedAddr>::iterator i;
    bool foundPendingWrite = false;

    // If there is a pending write to the same address
    // This store conditional will failed
    if (!doErase /*ignore this check at the 2nd pass*/){
        bool scPending = false;
        for (i = lockedAddrList.begin(); i != lockedAddrList.end(); ++i) {
            if (i->addr == paddr && i->writePending){
                if (isLLSC) {
                    foundPendingWrite = true;
                    req->setExtraData(0); // sc failed
                    DPRINTF(Octopus, "checkLockedAddrList found pending write SC failed: context %d addr %#x\n",
                        i->contextId, paddr);
                    break;
                }
                else if (i->lockBySC) {
                    scPending = true;
                    // Use the connection_id and port_id that were passed as parameters
                    assert(connection_id >= 0); // Make sure we have a valid connection_id
                    assert(port_id >= 0);       // Make sure we have a valid port_id
                    DPRINTF(Octopus, "Delaying normal write to addr 0x%x from connection %d port %d due to pending SC\n", 
                             paddr, connection_id, port_id);                    
                    // Create a function pointer that captures this pointer properly
                    auto func_ptr = [this](PacketPtr pkt, int conn_id, int port_id) -> void {
                        this->accessTiming(pkt, conn_id, port_id);
                    };
                    
                    // Queue the write to be processed later with the function pointer
                    i->delayedWrites.emplace_back(pkt, connection_id, port_id, func_ptr);
                }
            }
        }
        if (scPending) {
            // Don't allow the write to proceed now, it's been queued
            return false;
        }
    }

    // Iterate over list.  Note that there could be multiple matching records,
    // as more than one context could have done a load locked to this location.
    // Only remove records when we succeed in finding a record for (xc, addr);
    // then, remove all records with this address.  Failed store-conditionals do
    // not blow unrelated reservations.
    i = lockedAddrList.begin();

    if (isLLSC && !foundPendingWrite) {
        while (i != lockedAddrList.end()) {
            if (i->addr == paddr && i->matchesContext(req)) {
                // it's a store conditional, and as far as the memory system can
                // tell, the requesting context's lock is still valid.
                allowStore = true;
                break;
            }
            // If we didn't find a match, keep searching!  Someone else may well
            // have a reservation on this line here but we may find ours in just
            // a little while.
            i++;
        }
        req->setExtraData(allowStore ? 1 : 0);
        DPRINTF(Octopus, "StCond %s during %s: context %d addr %#x\n",
                        allowStore ? "success" : "fail", doErase? "Erase": "Set writePending",
                        req->contextId(), paddr);
        assert(allowStore || !doErase); // at response stage all SC should success
    }

    vector<DelayedWrite> processedDelayedWrites;
    // LLSCs that succeeded AND non-LLSC stores both fall into here:
    if (allowStore) {
        // We write address paddr.  However, there may be several entries with a
        // reservation on this address (for other contextIds) and they must all
        // be removed.
        i = lockedAddrList.begin();
        while (i != lockedAddrList.end()) {
            if (i->addr == paddr) {
                DPRINTF(Octopus, "%s lock record: context %d addr %#x\n",
                    doErase? "Erase": "Set writePending", i->contextId, paddr);
                ContextID owner_cid = i->contextId;
                assert(owner_cid != InvalidContextID);
                if (doErase){
                    ContextID requestor_cid = req->hasContextId() ?
                                                req->contextId() :
                                                InvalidContextID;
                    if (owner_cid != requestor_cid) {
                        ThreadContext* ctx = system->threads[owner_cid];
                        if (ctx->status() != ThreadContext::Active)
                            ctx->getIsaPtr()->globalClearExclusive(); // ignore waking active thread
                    }
                    
                    // Collect any delayed writes before erasing this lock record
                    if (!i->delayedWrites.empty()) {
                        DPRINTF(Octopus, "Collecting %d delayed writes for addr 0x%x\n", 
                                i->delayedWrites.size(), paddr);
                        // Collect all delayed writes for this address
                        for (auto& delayedWrite : i->delayedWrites) {
                            assert(LockedAddr::mask(delayedWrite.pkt->req->getPaddr()) == paddr);
                            // Use find with a lambda to check if this pkt is already in processedDelayedWrites
                            auto it = std::find_if(processedDelayedWrites.begin(), processedDelayedWrites.end(),
                                [&delayedWrite](const DelayedWrite& processed) {
                                    return processed.pkt == delayedWrite.pkt;
                                });
                            
                            // If not found, add it to processedDelayedWrites
                            if (it == processedDelayedWrites.end()) {
                                processedDelayedWrites.push_back(delayedWrite);
                            }
                        }
                    }
                    i = lockedAddrList.erase(i);
                }
                else {
                    i->writePending = true;
                    i->lockBySC = isLLSC;
                    i++;
                }
            } else {
                i++;
            }
        }
        
        // Now process all collected delayed writes after the erase is done
        if (doErase && !processedDelayedWrites.empty()) {
            for (auto& delayedWrite : processedDelayedWrites) {
                DPRINTF(Octopus, "Processing delayed write to addr 0x%x from connection %d port %d\n",
                        LockedAddr::mask(delayedWrite.pkt->req->getPaddr()), 
                        delayedWrite.connection_id, delayedWrite.port_id);
                
                // Use the function pointer instead of directly calling accessTiming
                delayedWrite.func_ptr(delayedWrite.pkt, delayedWrite.connection_id, delayedWrite.port_id);
            }
        }
    }

    return allowStore;
}

// Compare a store address with any locked addresses so we can
// clear the lock flag appropriately.  Return value set to 'false'
// if store operation should be suppressed (because it was a
// conditional store and the address was no longer locked by the
// requesting execution context), 'true' otherwise.  Note that
// this method must be called on *all* stores since even
// non-conditional stores must clear any matching lock addresses.
bool
Octopus::writeOK(PacketPtr pkt, bool doErase, int connection_id, int port_id)
{
    const RequestPtr &req = pkt->req;
    if (lockedAddrList.empty()) {
        // no locked addrs: nothing to check, store_conditional fails
        bool isLLSC = pkt->isLLSC();
        if (isLLSC) {
            req->setExtraData(0);
        }
        return !isLLSC; // only do write if not an sc
    } else {
        // iterate over list...
        return checkLockedAddrList(pkt, doErase, connection_id, port_id);
    }
}


} // namespace gem5
