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
uint64_t Octopus::num_pending_req = 0;

Octopus::Octopus(const OctopusParams &params) :
    ClockedObject(params),
    system(params.system),
    tickEvent([this]{ tick(); }, name()),
    blockSize(params.system->cacheLineSize()),
    reqFIFOSize(params.req_fifo_size),
    outstandingReqs(0),
    memPort(params.name + ".mem_side", this)
{
    // Since the CPU side ports are a vector of ports, create an instance of
    // the CPUSidePort for each connection. This member of params is
    // automatically created depending on the name of the vector port and
    // holds the number of connections to this port name
    cout << "Octopus " << params.config_file_path.c_str() <<" "<<params.output_logs_path.c_str() << endl;
    if(Octopus::cache_sim == NULL)
        Octopus::cache_sim = new ns3::CacheSim(params.config_file_path.c_str(),
                                      params.output_logs_path.c_str());

    ns3::CPUCallback<Octopus, uint64_t, uint64_t, ns3::RequestType, uint8_t*>* cache_sim_callback =
        new ns3::CPUCallback<Octopus, uint64_t, uint64_t, ns3::RequestType, uint8_t*>(this, &Octopus::cacheSimCallback);
    ns3::ExternalCPU::getExtCPUs()->at(params.cache_id)->registerCPUCallback(cache_sim_callback);

    ns3::MemCallback<Octopus, uint64_t, uint64_t, ns3::RequestType, uint8_t*>* cache_sim_mem_callback =
        new ns3::MemCallback<Octopus, uint64_t, uint64_t, ns3::RequestType, uint8_t*>(this, &Octopus::cacheSimMemCallback);
    // ns3::ExternalMem::getExtMem()->registerMemCallback(params.cache_id, cache_sim_mem_callback);

    for (int i = 0; i < params.port_cpu_side_connection_count; ++i) {
        cpuPorts.emplace_back(name() + csprintf(".cpu_side[%d]", i),"CpuSidePort", params.cache_id, i, this);
    }
    DPRINTF(Octopus, "Connect to cache id %d\n", params.cache_id);
}

void Octopus::cacheSimCallback(uint64_t address, uint64_t cycle, ns3::RequestType type, uint8_t* data)
{
    auto itr = std::find_if(pending_requests.begin(), pending_requests.end(),
                            [&](pair<PacketPtr, pair<int,int>> entry) -> bool{
                                return entry.first->getAddr() == address;
                            });
    assert(itr != pending_requests.end()); // we should always find a coresponding pkt
    PacketPtr pkt = itr->first;
    int connection_id = itr->second.first;
    int port_id = itr->second.second;

    bool swapCompleted = type == ns3::RequestType::WRITE;
    bool isSwap = pkt->cmd == MemCmd::SwapReq;
    if (isSwap && !swapCompleted){
        ns3::ExternalCPU::getExtCPUs()->at(connection_id)->addRequest(pkt->req->getPaddr(), ns3::RequestType::WRITE, NULL /*Do not attenpt to get ptr to avoid masked write assertion*/, pkt->getSize());
    }

    if (!isSwap || swapCompleted){
        pending_requests.erase(itr);
        num_pending_req--;
        outstandingReqs--;
        assert(pkt->isResponse());
        cpuPorts[port_id].sendPacket(pkt);
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
                            [&](pair<PacketPtr, pair<int,int>> entry) -> bool{
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
    }
    else
    {
        PacketPtr pkt = itr->first;
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
    memPort.sendPacket(new_pkt);
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
    owner->handleAtomic(pkt, connection_id, id);

    // 1 ns is just an arbitrary value at this point
    return 1000;
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
    //     ns3::ExternalMem::getExtMem()->read_callback(pkt->req->getPaddr(), curTick());
    // else
    //     ns3::ExternalMem::getExtMem()->write_callback(pkt->req->getPaddr(), curTick());
    // delete pkt;

    return true;
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

bool
Octopus::accessTiming(PacketPtr pkt, int connection_id, int port_id)
{
    // Invalidatation and clean not implemented
    if (pkt->isInvalidate() || pkt->isClean()) {
        pkt->makeResponse();
        cpuPorts[port_id].sendPacket(pkt);
        return true;
    }

    memPort.sendAtomic(pkt);
    assert(pkt->isResponse());

    // Do not forward failed LLSC write to octopus
    const RequestPtr &req = pkt->req;
    if (pkt->isLLSC() && pkt->isWrite() && req->getExtraData() == 0) {
        cpuPorts[port_id].sendPacket(pkt);
        return true;
    }

    if (outstandingReqs >= reqFIFOSize) {
        return false;
    }

    if (pkt->isRead()) {
        ns3::ExternalCPU::getExtCPUs()->at(connection_id)->addRequest(pkt->req->getPaddr(), ns3::RequestType::READ, NULL, pkt->getSize());
        pending_requests.push_back(std::make_pair(pkt, std::make_pair(connection_id,port_id)));
        num_pending_req++;
        outstandingReqs++;
    }
    else if (pkt->isWrite()) {
        ns3::ExternalCPU::getExtCPUs()->at(connection_id)->addRequest(pkt->req->getPaddr(), ns3::RequestType::WRITE, NULL /*Do not attenpt to get ptr to avoid masked write assertion*/, pkt->getSize());
        pending_requests.push_back(std::make_pair(pkt, std::make_pair(connection_id,port_id)));
        num_pending_req++;
        outstandingReqs++;
    }
    return true;
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

} // namespace gem5

