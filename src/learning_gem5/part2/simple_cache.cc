/*
 * Copyright (c) 2017 Jason Lowe-Power
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

#include "learning_gem5/part2/simple_cache.hh"

#include "base/compiler.hh"
#include "base/random.hh"
#include "debug/SimpleCache.hh"
#include "sim/system.hh"

#define external_cache

namespace gem5
{

int SimpleCache::connection_id = 0;
ns3::CacheSim* SimpleCache::cache_sim = NULL;
bool SimpleCache::ext_cache_started = false;

map<int, pair<int, vector<SimpleCache::LockEntry>*>> SimpleCache::RMW_lock_map;
std::set<string> SimpleCache::debug_set;

SimpleCache::SimpleCache(const SimpleCacheParams &params) :
    ClockedObject(params),
    latency(params.latency),
    blockSize(params.system->cacheLineSize()),
    capacity(params.size / blockSize),
    memPort(params.name + ".mem_side", this),
    blocked(false), originalPacket(nullptr), waitingPortId(-1), stats(this),
    tickEvent([this]{ tick(); }, name())
{
    // Since the CPU side ports are a vector of ports, create an instance of
    // the CPUSidePort for each connection. This member of params is
    // automatically created depending on the name of the vector port and
    // holds the number of connections to this port name
    cout << "simpleCache" << endl;
    if(SimpleCache::cache_sim == NULL)
        SimpleCache::cache_sim = new ns3::CacheSim(params.config_file_path.c_str(),
                                      "/home/gem5/gem5_new/ext/CMSpec/CMSpec/BMs/");
    // cache_sim->run();

    ns3::CPUCallback<SimpleCache, uint64_t, uint64_t, ns3::RequestType, uint8_t*>* cache_sim_callback =
        new ns3::CPUCallback<SimpleCache, uint64_t, uint64_t, ns3::RequestType, uint8_t*>(this, &SimpleCache::cacheSimCallback);
    ns3::ExternalCPU::getExtCPUs()->at(SimpleCache::connection_id)->registerCPUCallback(cache_sim_callback);

    // ns3::MemCallback<SimpleCache, uint64_t, uint64_t, ns3::RequestType, uint8_t*>* cache_sim_mem_callback =
    //     new ns3::MemCallback<SimpleCache, uint64_t, uint64_t, ns3::RequestType, uint8_t*>(this, &SimpleCache::cacheSimMemCallback);
    // ns3::ExternalMem::getExtMem()->registerMemCallback(SimpleCache::connection_id, cache_sim_mem_callback);

    for (int i = 0; i < params.port_cpu_side_connection_count; ++i) {
        cpuPorts.emplace_back(name() + csprintf(".cpu_side[%d]", i), SimpleCache::connection_id, i, this);
    }

    SimpleCache::connection_id++;
}

void SimpleCache::print2file(PacketPtr pkt, bool functional, bool i_port)
{
    static int count[6] = {0};
    static ofstream myfile_F[2];
    static ofstream myfile_D[2];
    static ofstream myfile_I[2];
    ofstream* myfile;

    if(!myfile_F[0].is_open())
        myfile_F[0].open ("output_octo_F0.txt");
    if(!myfile_F[1].is_open())
        myfile_F[1].open ("output_octo_F1.txt");
    if(!myfile_D[0].is_open())
        myfile_D[0].open ("output_octo_D0.txt");
    if(!myfile_D[1].is_open())
        myfile_D[1].open ("output_octo_D1.txt");
    if(!myfile_I[0].is_open())
        myfile_I[0].open ("output_octo_I0.txt");
    if(!myfile_I[1].is_open())
        myfile_I[1].open ("output_octo_I1.txt");


    if(functional)
    {
        if(this->name().find("caches0") != std::string::npos)
        {
            myfile = &myfile_F[0];
            // count[0]++;
            (*myfile) << "Functional[" << std::dec << count[0] << "]";
        }
        else
        {
            myfile = &myfile_F[1];
            // count[1]++;
            (*myfile) << "Functional[" << std::dec << count[1] << "]";
        }

        // if(count[0] == 24923)
        //     cout << "here\n";
    }
    else
    {
        if(i_port)
        {
            if(this->name().find("caches0") != std::string::npos)
            {
                myfile = &myfile_I[0];
                // count[2]++;
                (*myfile) << "response[" << std::dec << count[2] << "]";
            }
            else
            {
                myfile = &myfile_I[1];
                // count[3]++;
                (*myfile) << "response[" << std::dec << count[3] << "]";
            }
        }
        else
        {
            if(this->name().find("caches0") != std::string::npos)
            {
                myfile = &myfile_D[0];
                // count[4]++;
                (*myfile) << "response[" << std::dec << count[4] << "]";
            }
            else
            {
                myfile = &myfile_D[1];
                // count[5]++;
                (*myfile) << "response[" << std::dec << count[5] << "]";
            }
        }
    }

    switch(pkt->getSize())
    {
        case 1:
            (*myfile) << " = 0x" << std::hex << unsigned(*(pkt->getPtr<uint8_t>()));
        break;
        case 2:
            (*myfile) << " = 0x" << std::hex << unsigned(*(pkt->getPtr<uint16_t>()));
        break;
        case 4:
            (*myfile) << " = 0x" << std::hex << unsigned(*(pkt->getPtr<uint32_t>()));
        break;
        case 8:
            (*myfile) << " = 0x" << std::hex << unsigned(*(pkt->getPtr<uint64_t>()));
        break;
        default:
            (*myfile) << " = LARGE -> 0x" << std::hex << unsigned(*(pkt->getPtr<uint64_t>()));
        break;
    }
    if(functional)
        (*myfile) << std::hex << "(0x" << pkt->getAddr() << ")" << " Read->" << pkt->isRead() << "+" << pkt->getSize();
    else
    {
        (*myfile) << std::hex << "(paddr 0x" << pkt->getAddr() << ") (vaddr 0x" << pkt->req->getVaddr() << ")";
        (*myfile) << " Read->" << pkt->isRead() << "+" << pkt->getSize();
    }
    // (*myfile) << "\t" << this->name() << endl;
    (*myfile) << std::dec << " (tick = " << curTick() << ")"<< endl;
}

void SimpleCache::cacheSimCallback(uint64_t address, uint64_t cycle, ns3::RequestType type, uint8_t* data)
{
    // cout<< "Call back!!" << count <<endl;

    // if(pending_requests.find(address) == pending_requests.end())
    //     return;
    // PacketPtr pkt = pending_requests[address].first;
    // int port_id = pending_requests[address].second;
    // pending_requests.erase(address);
    auto itr = std::find_if(pending_requests.begin(), pending_requests.end(),
                            [&](pair<PacketPtr, int> entry) -> bool{
                                return entry.first->getAddr() == address;
                            });
    PacketPtr pkt = itr->first;
    int port_id = itr->second;
    pending_requests.erase(itr);

    // if (pkt->isWrite()) //should get data from the callback
    // {
    //     // Write the data into the block in the cache
    //     //pkt->writeDataToBlock(it->second, blockSize);
    // }
    // else if (pkt->isRead())
    // {
    //     // Read the data out of the cache block into the packet
    //     // pkt->setDataFromBlock(data, blockSize);
    //     //cout << "response[" << std::dec << count << "] = 0x" << std::hex << (*(pkt->getPtr<uint64_t>())) << "(0x" << address << ")" << endl;
    // }
    // else
    //     panic("Unknown packet type!");

    memPort.sendFunctional(pkt);

    // print2file(pkt, false, (port_id == 0));
    // pkt->makeResponse();
    this->sendResponse(pkt, port_id);


    if(pkt->cmd == MemCmd::LockedRMWWriteResp)
    {
        auto vect = RMW_lock_map[pkt->getAddr() & ~(blockSize-1)].second;

        auto it = RMW_lock_map.find(pkt->getAddr() & ~(blockSize-1));
        RMW_lock_map.erase(it);

        // cout << "Release line @ = " << std::hex << (pkt->getAddr() & ~(blockSize-1)) << endl;
        for(auto v : *vect)
            v.func_ptr(v.pkt, v.connection_id, v.port_id);

        delete vect;
    }

    // myfile << "response[" << std::dec << count << "] \t" << "(0x" << address << ")"
    //        << " Read->" << pkt->isRead() << "+" << pkt->getSize() << endl;
}

void SimpleCache::cacheSimMemCallback(uint64_t address, uint64_t cycle, ns3::RequestType type, uint8_t* data)
{
    if (type == ns3::RequestType::SETUP_READ || type == ns3::RequestType::SETUP_WRITE)
    {
        assert("receive functional request\n");
    }

    bool isMemRead = data == NULL;
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
    }
    // DPRINTF(SimpleCache, "forwarding packet\n");
    memPort.sendPacket(new_pkt);
    // cout << "cacheSimMemCallback send " <<std::hex << new_pkt->print()<< " "<< this<< endl;
}

Port &
SimpleCache::getPort(const std::string &if_name, PortID idx)
{
    // This is the name from the Python SimObject declaration in SimpleCache.py
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
SimpleCache::CPUSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    DPRINTF(SimpleCache, "Sending %s to CPU\n", pkt->print());
    if (!sendTimingResp(pkt)) {
        DPRINTF(SimpleCache, "failed!\n");
        blockedPacket = pkt;
    }
}

AddrRangeList
SimpleCache::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
SimpleCache::CPUSidePort::trySendRetry()
{
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(SimpleCache, "Sending retry req.\n");
        sendRetryReq();
    }
}

void
SimpleCache::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    // cout << "Functional read: "<< pkt->isRead() << endl;
    // Just forward to the cache.
    owner->handleFunctional(pkt, connection_id);
    if(pkt->getAddr() == 0xbe308)
        cout << "HERE\n";

    // owner->print2file(pkt, true);
}

bool
SimpleCache::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(SimpleCache, "Got request %s\n", pkt->print());
    if(pkt->req->isUncacheable())
        cout<<"here\n";
    if (blockedPacket || needRetry) {
        // The cache may not be able to send a reply if this is blocked
        DPRINTF(SimpleCache, "Request blocked\n");
        needRetry = true;
        return false;
    }
    // Just forward to the cache.
    if (!owner->handleRequest(pkt, connection_id, id)) {
        DPRINTF(SimpleCache, "Request failed\n");
        // stalling
        needRetry = true;
        return false;
    } else {
        DPRINTF(SimpleCache, "Request succeeded\n");
        return true;
    }
}

void
SimpleCache::CPUSidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    DPRINTF(SimpleCache, "Retrying response pkt %s\n", pkt->print());
    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);

    // We may now be able to accept new packets
    trySendRetry();
}

void
SimpleCache::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the cache is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

bool
SimpleCache::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the cache.
    return owner->handleResponse(pkt);
}

void
SimpleCache::MemSidePort::recvReqRetry()
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
SimpleCache::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

bool
SimpleCache::handleRequest(PacketPtr pkt, int connection_id, int port_id)
{

#ifndef external_cache
    if (blocked) {
        // There is currently an outstanding request so we can't respond. Stall
        return false;
    }

    DPRINTF(SimpleCache, "Got request for addr %#x\n", pkt->getAddr());

    // This cache is now blocked waiting for the response to this packet.
    blocked = true;

    // Store the port for when we get the response
    assert(waitingPortId == -1);
    waitingPortId = port_id;

    // Schedule an event after cache access latency to actually access
    schedule(new EventFunctionWrapper([this, pkt]{ accessTiming(pkt, 0); },
                                      name() + ".accessEvent", true),
             clockEdge(latency));
#else
    debug_set.insert(pkt->cmd.toString());

    accessTiming(pkt, connection_id, port_id);
#endif

    return true;
}

bool
SimpleCache::handleResponse(PacketPtr pkt)
{
#ifndef external_cache
    assert(blocked);
    DPRINTF(SimpleCache, "Got response for addr %#x\n", pkt->getAddr());
    // For now assume that inserts are off of the critical path and don't count
    // for any added latency.
    insert(pkt);

    stats.missLatency.sample(curTick() - missTime);

    // If we had to upgrade the request packet to a full cache line, now we
    // can use that packet to construct the response.
    if (originalPacket != nullptr) {
        DPRINTF(SimpleCache, "Copying data from new packet to old\n");
        // We had to upgrade a previous packet. We can functionally deal with
        // the cache access now. It better be a hit.
        [[maybe_unused]] bool hit = accessFunctional(originalPacket);
        panic_if(!hit, "Should always hit after inserting");
        originalPacket->makeResponse();
        delete pkt; // We may need to delay this, I'm not sure.
        pkt = originalPacket;
        originalPacket = nullptr;
    } // else, pkt contains the data it needs

    sendResponse(pkt);
#else
    // print2file(pkt);
    // forward packet to octopus only if this request if send from octopus mem ctrl
    // bool isMemRead = pkt->isRead();

    // // cout << "handleResponse"<<std::hex <<pkt <<" " <<pkt->getAddr() << " "<< isMemRead <<" "<< pkt->needsResponse()<<" " <<this<<  endl;
    // if (isMemRead)
    //     ns3::ExternalMem::getExtMem()->read_callback(pkt->getAddr(), curTick());
    // else
    //     ns3::ExternalMem::getExtMem()->write_callback(pkt->getAddr(), curTick());
    // delete pkt;
#endif

    return true;
}

void SimpleCache::sendResponse(PacketPtr pkt, int port_id)
{
#ifndef external_cache
    assert(blocked);
    DPRINTF(SimpleCache, "Sending resp for addr %#x\n", pkt->getAddr());

    int port = waitingPortId;

    // The packet is now done. We're about to put it in the port, no need for
    // this object to continue to stall.
    // We need to free the resource before sending the packet in case the CPU
    // tries to send another request immediately (e.g., in the same callchain).
    blocked = false;
    waitingPortId = -1;

    // Simply forward to the memory port
    cpuPorts[port].sendPacket(pkt);
#else
    // Simply forward to the memory port
    cpuPorts[port_id].sendPacket(pkt);
#endif


    // For each of the cpu ports, if it needs to send a retry, it should do it
    // now since this memory object may be unblocked now.
    for (auto& port : cpuPorts) {
        port.trySendRetry();
    }
}

void
SimpleCache::handleFunctional(PacketPtr pkt, int connection_id)
{
#ifndef external_cache
    if (accessFunctional(pkt)) {
        pkt->makeResponse();
    } else {
        memPort.sendFunctional(pkt);
        if(pkt->isRead())
        {
            // for(int i = 0; i < 64; i++)
            // {
            //     cout << "data[" << i << "] = " << (unsigned) pkt->getConstPtr<uint8_t>()[i] << endl;
            // }
            // cout << "print" << endl;
        }
    }
#else
    if(pkt->isRead())
    {
        // uint8_t data[blockSize];
        // // uint8_t data2[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x70, 0x3c,
        // //                         0x71, 0xf5, 0xff, 0x7f, 0x0, 0x0, 0x30, 0x3d, 0x71, 0xf5, 0xff, 0x7f, 0x0, 0x0, 0x30, 0x3f, 0x71, 0xf5, 0xff,
        // //                         0x7f, 0x0, 0x0, 0x70, 0x3f, 0x71, 0xf5, 0xff, 0x7f, 0x0, 0x0};
        // memset(data, 0, blockSize);

        // if(pkt->getAddr() != 0x94ba0)
        // ns3::ExternalCPU::getExtCPUs()->at(connection_id)->addRequest(pkt->getAddr(), ns3::RequestType::SETUP_READ, data, pkt->getSize());


        // if(pkt->getAddr() != 0x94ba0)
        // pkt->setDataFromBlock(data, blockSize);
        // else
        //     pkt->setDataFromBlock(data2, blockSize);
        // pkt->makeResponse();
        // for(int i = 0; i < 64; i++)
        // {
        //     cout << "data[" << i << "] = " << (unsigned) data[i] << endl;
        // }
    }
    else
    {
        // if(!ext_cache_started)
            // ns3::ExternalCPU::getExtCPUs()->at(connection_id)->addRequest(pkt->getAddr(), ns3::RequestType::SETUP_WRITE, (uint8_t*)pkt->getConstPtr<uint8_t>(), pkt->getSize());
        // else
        //     ns3::ExternalCPU::getExtCPUs()->at(connection_id)->addRequest(pkt->getAddr(), ns3::RequestType::WRITE, pkt->getPtr<uint8_t>(), pkt->getSize());
    }
    memPort.sendFunctional(pkt);
#endif
}

void
SimpleCache::accessTiming(PacketPtr pkt, int connection_id, int port_id)
{
    static int count = 0;
    // cout << "request num = " << count << endl;
    count++;

#ifndef external_cache
    bool hit = accessFunctional(pkt);

    DPRINTF(SimpleCache, "%s for packet: %s\n", hit ? "Hit" : "Miss",
            pkt->print());

    if (hit) {
        // Respond to the CPU side
        stats.hits++; // update stats
        DDUMP(SimpleCache, pkt->getConstPtr<uint8_t>(), pkt->getSize());
        pkt->makeResponse();
        sendResponse(pkt);
        print2file(pkt);

        // switch(pkt->getSize())
        // {
        //     case 1:
        //         SimpleCache::myfile << "response[" << std::dec << count << "] = 0x" << std::hex << unsigned(*(pkt->getPtr<uint8_t>()))
        //             << "(0x" << pkt->getAddr() << ")" << " Read->" << pkt->isRead() << "+" << pkt->getSize() << endl;
        //     break;
        //     case 2:
        //         SimpleCache::myfile << "response[" << std::dec << count << "] = 0x" << std::hex << unsigned(*(pkt->getPtr<uint16_t>()))
        //             << "(0x" << pkt->getAddr() << ")" << " Read->" << pkt->isRead() << "+" << pkt->getSize() << endl;
        //     break;
        //     case 4:
        //         SimpleCache::myfile << "response[" << std::dec << count << "] = 0x" << std::hex << unsigned(*(pkt->getPtr<uint32_t>()))
        //             << "(0x" << pkt->getAddr() << ")" << " Read->" << pkt->isRead() << "+" << pkt->getSize() << endl;
        //     break;
        //     case 8:
        //         SimpleCache::myfile << "response[" << std::dec << count << "] = 0x" << std::hex << unsigned(*(pkt->getPtr<uint64_t>()))
        //             << "(0x" << pkt->getAddr() << ")" << " Read->" << pkt->isRead() << "+" << pkt->getSize() << endl;
        //     break;
        // }

    } else {
        stats.misses++; // update stats
        missTime = curTick();
        // Forward to the memory side.
        // We can't directly forward the packet unless it is exactly the size
        // of the cache line, and aligned. Check for that here.
        Addr addr = pkt->getAddr();
        Addr block_addr = pkt->getBlockAddr(blockSize);
        unsigned size = pkt->getSize();
        if (addr == block_addr && size == blockSize) {
            // Aligned and block size. We can just forward.
            DPRINTF(SimpleCache, "forwarding packet\n");
            memPort.sendPacket(pkt);
        } else {
            DPRINTF(SimpleCache, "Upgrading packet to block size\n");
            panic_if(addr - block_addr + size > blockSize,
                     "Cannot handle accesses that span multiple cache lines");
            // Unaligned access to one cache block
            assert(pkt->needsResponse());
            MemCmd cmd;
            if (pkt->isWrite() || pkt->isRead()) {
                // Read the data from memory to write into the block.
                // We'll write the data in the cache (i.e., a writeback cache)
                cmd = MemCmd::ReadReq;
            } else {
                panic("Unknown packet type in upgrade size");
            }

            // Create a new packet that is blockSize
            PacketPtr new_pkt = new Packet(pkt->req, cmd, blockSize);
            new_pkt->allocate();

            // Should now be block aligned
            assert(new_pkt->getAddr() == new_pkt->getBlockAddr(blockSize));

            // Save the old packet
            originalPacket = pkt;

            DPRINTF(SimpleCache, "forwarding packet\n");
            memPort.sendPacket(new_pkt);
        }
    }
#else

    if(RMW_lock_map.find(pkt->getAddr() & ~(blockSize-1)) != RMW_lock_map.end() &&
        RMW_lock_map[pkt->getAddr() & ~(blockSize-1)].first != connection_id)
    {
        LockEntry entry;
        entry.pkt = pkt; entry.connection_id = connection_id; entry.port_id = port_id;
        entry.func_ptr = [&](PacketPtr pkt, int connection_id, int port_id)->void
                            {
                                // cout << "callback on line @ = " << std::hex << (pkt->getAddr() & ~(blockSize-1))
                                //     << " from core: " << connection_id << endl;

                                this->accessTiming(pkt, connection_id, port_id);
                            };

        RMW_lock_map[pkt->getAddr() & ~(blockSize-1)].second->push_back(entry);

        // cout << "Waiting line @ = " << std::hex << (pkt->getAddr() & ~(blockSize-1))
        //      << " from core: " << connection_id << endl;

        return;
    }

    if(pkt->cmd == MemCmd::LockedRMWReadReq)
    {
        RMW_lock_map[pkt->getAddr() & ~(blockSize-1)] =
                std::make_pair(connection_id, new vector<LockEntry>);
        // cout << "Lock line @ = " << std::hex << (pkt->getAddr() & ~(blockSize-1))
        //      << " from core: " << connection_id << endl;
    }

    if (pkt->isWrite())
        ns3::ExternalCPU::getExtCPUs()->at(connection_id)->addRequest(pkt->getAddr(), ns3::RequestType::WRITE, pkt->getPtr<uint8_t>(), pkt->getSize());
    else if (pkt->isRead())
        ns3::ExternalCPU::getExtCPUs()->at(connection_id)->addRequest(pkt->getAddr(), ns3::RequestType::READ, NULL, -1);
    else
        panic("Unknown packet type!");

    // myfile << "add_request[" << std::dec << count << "] \t" << std::hex << "(0x" << pkt->getAddr() << ")"
    //        << " Read->" << pkt->isRead() << "+" << pkt->getSize() << "\tcpu = " << connection_id << endl;
    // if(count == 967452)
    //     cout << "here\n";
    // pending_requests[pkt->getAddr()] = std::make_pair(pkt, port_id);
    pending_requests.push_back(std::make_pair(pkt, port_id));
#endif
}

bool
SimpleCache::accessFunctional(PacketPtr pkt)
{
    Addr block_addr = pkt->getBlockAddr(blockSize);
    auto it = cacheStore.find(block_addr);
    if (it != cacheStore.end()) {
        if (pkt->isWrite()) {
            // Write the data into the block in the cache
            pkt->writeDataToBlock(it->second, blockSize);
        } else if (pkt->isRead()) {
            // Read the data out of the cache block into the packet
            pkt->setDataFromBlock(it->second, blockSize);
        } else {
            panic("Unknown packet type!");
        }
        return true;
    }
    return false;
}

void
SimpleCache::insert(PacketPtr pkt)
{
    // The packet should be aligned.
    assert(pkt->getAddr() ==  pkt->getBlockAddr(blockSize));
    // The address should not be in the cache
    assert(cacheStore.find(pkt->getAddr()) == cacheStore.end());
    // The pkt should be a response
    assert(pkt->isResponse());

    if (cacheStore.size() >= capacity) {
        // Select random thing to evict. This is a little convoluted since we
        // are using a std::unordered_map. See http://bit.ly/2hrnLP2
        int bucket, bucket_size;
        do {
            bucket = random_mt.random(0, (int)cacheStore.bucket_count() - 1);
        } while ( (bucket_size = cacheStore.bucket_size(bucket)) == 0 );
        auto block = std::next(cacheStore.begin(bucket),
                               random_mt.random(0, bucket_size - 1));

        DPRINTF(SimpleCache, "Removing addr %#x\n", block->first);

        // Write back the data.
        // Create a new request-packet pair
        RequestPtr req = std::make_shared<Request>(
            block->first, blockSize, 0, 0);

        PacketPtr new_pkt = new Packet(req, MemCmd::WritebackDirty, blockSize);
        new_pkt->dataDynamic(block->second); // This will be deleted later

        DPRINTF(SimpleCache, "Writing packet back %s\n", pkt->print());
        // Send the write to memory
        memPort.sendPacket(new_pkt);

        // Delete this entry
        cacheStore.erase(block->first);
    }

    DPRINTF(SimpleCache, "Inserting %s\n", pkt->print());
    DDUMP(SimpleCache, pkt->getConstPtr<uint8_t>(), blockSize);

    // Allocate space for the cache block data
    uint8_t *data = new uint8_t[blockSize];

    // Insert the data and address into the cache store
    cacheStore[pkt->getAddr()] = data;

    // Write the data into the cache
    pkt->writeDataToBlock(data, blockSize);
}

AddrRangeList
SimpleCache::getAddrRanges() const
{
    DPRINTF(SimpleCache, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return memPort.getAddrRanges();
}

void
SimpleCache::sendRangeChange() const
{
    for (auto& port : cpuPorts) {
        port.sendRangeChange();
    }
}

SimpleCache::SimpleCacheStats::SimpleCacheStats(statistics::Group *parent)
      : statistics::Group(parent),
      ADD_STAT(hits, statistics::units::Count::get(), "Number of hits"),
      ADD_STAT(misses, statistics::units::Count::get(), "Number of misses"),
      ADD_STAT(missLatency, statistics::units::Tick::get(),
               "Ticks for misses to the cache"),
      ADD_STAT(hitRatio, statistics::units::Ratio::get(),
               "The ratio of hits to the total accesses to the cache",
               hits / (hits + misses))
{
    missLatency.init(16); // number of buckets
}

void SimpleCache::tick()
{
    cache_sim->step();
    cache_sim->step();
    schedule(tickEvent, clockEdge(Cycles(1)));
}

void SimpleCache::startup()
{
    // kick off the clock ticks
    if(!SimpleCache::ext_cache_started)
        schedule(tickEvent, clockEdge(Cycles(1)));
    SimpleCache::ext_cache_started = true;
}


} // namespace gem5
