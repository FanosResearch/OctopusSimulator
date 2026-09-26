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

#ifndef __OCTOPUS_HH__
#define __OCTOPUS_HH__

#include <unordered_map>

#include "base/statistics.hh"
#include "mem/port.hh"
#include "mem/qport.hh"
#include "params/Octopus.hh"
#include "sim/clocked_object.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

#include <fstream>
#include <map>
#include <vector>
#include <functional>
#include <utility>
#include <set>
using namespace std;

// The Octopus side of the bridge. These used to be re-declared by hand here,
// which the compiler could not check against the library: a changed return
// type, enum value or virtual slot linked cleanly and failed at run time.
// Including the real headers makes any such mismatch a compile error.
#include "ExternalCPU.h"
#include "CacheSim.h"

namespace gem5
{
  class Octopus : public ClockedObject
  {
  public:
    System *system;

  private:
    static octopus::CacheSim *cache_sim;
    static bool ext_cache_started;
    static uint64_t num_pending_req;

    /**
     * Event to schedule clock ticks
     */
    EventFunctionWrapper tickEvent;

    /**
     * A cache response port is used for the CPU-side port of the cache,
     * and it is basically a simple timing port that uses a transmit
     * list for responses to the CPU (or connected requestor). In
     * addition, it has the functionality to block the port for
     * incoming requests. If blocked, the port will issue a retry once
     * unblocked.
     */
    class CPUSidePort : public QueuedResponsePort
    {

      public:
        /**
         * Constructor. Just calls the superclass constructor.
         */
        CPUSidePort(const std::string &_name, const std::string &_label, int connection_id, int id, Octopus *owner)
            : QueuedResponsePort(_name, queue),
              connection_id(connection_id),id(id), owner(owner),
              queue(*owner, *this, true, _label)
        {
        }
        /**
         * Send a packet across this port. This is called by the owner and
         * all of the flow control is hanled in this function.
         * This is a convenience function for the Octopus to send pkts.
         *
         * @param packet to send.
         */
        void sendPacket(PacketPtr pkt);

        /**
         * Get a list of the non-overlapping address ranges the owner is
         * responsible for. All response ports must override this function
         * and return a populated list with at least one item.
         *
         * @return a list of ranges responded to
         */
        AddrRangeList getAddrRanges() const override;

      protected:
        virtual bool recvTimingSnoopResp(PacketPtr pkt) override
          {
          panic("recvTimingSnoopResp unimpl.");
          }

        /**
         * Receive an atomic request packet from the request port.
         * No need to implement in this simple cache.
         */
        Tick recvAtomic(PacketPtr pkt) override;

        /**
         * Receive a functional request packet from the request port.
         * Performs a "debug" access updating/reading the data in place.
         *
         * @param packet the requestor sent.
         */
        void recvFunctional(PacketPtr pkt) override;

        /**
         * Receive a timing request from the request port.
         *
         * @param the packet that the requestor sent
         * @return whether this object can consume to packet. If false, we
         *         will call sendRetry() when we can try to receive this
         *         request again.
         */
        bool recvTimingReq(PacketPtr pkt) override;
      public:
        /**
         * Check if this port needs a retry.
         * @return true if a retry is needed
         */
        bool needsRetry() const { return needRetry; }

        /**
         * Send a retry request to this port.
         */
        void sendRetryReq() {
            needRetry = false;
            ResponsePort::sendRetryReq();
        }

      private:
        /// Since this is a vector port, need to know what number this one is
        int connection_id;
        int id;
        
        /// The object that owns this object (Octopus)
        Octopus *owner;

        /** A normal packet queue used to store responses. */
        RespPacketQueue queue;

        /// Track if this port needs a retry
        bool needRetry = false;

        // Allow owner to set the retry flag
        friend class Octopus;
    };

    /**
     * Port on the memory-side that receives responses.
     * Mostly just forwahrds requests to the cache (owner)
     */
    class MemSidePort : public RequestPort
    {
    private:
      /// The object that owns this object (Octopus)
      Octopus *owner;

      /// If we tried to send a packet and it was blocked, store it here
      PacketPtr blockedPacket;

    public:
      /**
       * Constructor. Just calls the superclass constructor.
       */
      MemSidePort(const std::string &name, Octopus *owner) : RequestPort(name), owner(owner), blockedPacket(nullptr)
      {
      }

      /**
       * Send a packet across this port. This is called by the owner and
       * all of the flow control is hanled in this function.
       * This is a convenience function for the Octopus to send pkts.
       *
       * @param packet to send.
       */
      void sendPacket(PacketPtr pkt);

    protected:
      /**
       * Receive a timing response from the response port.
       */
      bool recvTimingResp(PacketPtr pkt) override;

      /**
       * Called by the response port if sendTimingReq was called on this
       * request port (causing recvTimingReq to be called on the response
       * port) and was unsuccesful.
       */
      void recvReqRetry() override;

      /**
       * Called to receive an address range change from the peer response
       * port. The default implementation ignores the change and does
       * nothing. Override this function in a derived class if the owner
       * needs to be aware of the address ranges, e.g. in an
       * interconnect component like a bus.
       */
      void recvRangeChange() override;
    };

    /**
     * Handle the respone from the memory side. Called from the memory port
     * on a timing response.
     *
     * @param responding packet
     * @return true if we can handle the response this cycle, false if the
     *         responder needs to retry later
     */
    bool handleResponse(PacketPtr pkt);

    /**
     * Handle a packet functionally. Update the data on a write and get the
     * data on a read. Called from CPU port on a recv functional.
     *
     * @param packet to functionally handle
     */
    void handleFunctional(PacketPtr pkt, int connection_id);

    /**
     * Handle a atomic packet. This is called from the CPU port on a recvAtomic.
     *
     * @param packet
     */
    Tick handleAtomic(PacketPtr pkt, int connection_id, int port_id);

    /**
     * Access the cache for a timing access. This is called after the cache
     * access latency has already elapsed.
     */
    bool accessTiming(PacketPtr pkt, int connection_id, int port_id);

    /**
     * Return the address ranges this cache is responsible for. Just use the
     * same as the next upper level of the hierarchy.
     *
     * @return the address ranges this cache is responsible for
     */
    AddrRangeList getAddrRanges() const;

    /**
     * Tell the CPU side to ask for our memory ranges.
     */
    void sendRangeChange() const;

    /**
     * Helper function to get address from packet.
     * Returns physical address if available, otherwise returns virtual address.
     */
    Addr getAddr(PacketPtr pkt) const;

    void cacheSimCallback(uint64_t msg_id, uint64_t address, uint64_t cycle, octopus::RequestType type, uint8_t* data);


    /// The block size for the cache
    const unsigned blockSize;


    /// Octopus ns to advance per gem5 clock cycle.
    const unsigned octopusCycleNs;


    /// The Octopus adaptor (ExternalCPU) that feeds this bridge's L1.
    octopus::ExternalCPU *extCpu;

    /// CPU-port refusals while the L1 was blocked.
    uint64_t portBlocks = 0;

    /// Every bridge, so the one driving the Octopus clock can poll them all.
    static std::vector<Octopus*> instances;

    /// Instantiation of the CPU-side port
    std::vector<CPUSidePort> cpuPorts;

    /// Instantiation of the memory-side port
    MemSidePort memPort;

    /// One accepted CPU request, from acceptance until its response is sent.
    /// The packet stays a request until commit.
    struct PendingTxn
    {
        PacketPtr pkt;
        int connection_id;
        int port_id;
        /// Set by onCommit(): Octopus has serialised this request.
        bool committed;
        /// Acceptance order on this bridge.
        uint64_t seq;
        /// The data operation has been performed; the packet is a response.
        bool dataDone;
        /// The engine's completion arrived while the data operation was still
        /// held back; respond as soon as it has been performed.
        bool completionArrived;
    };

    uint64_t nextSeq = 0;
    /// Data operations held back behind an older overlapping write.
    uint64_t heldBackOps = 0;

    /// Accepted requests, keyed by the msg_id ExternalCPU::addRequest()
    /// returned. Matching completions by address picks the wrong entry as
    /// soon as two requests to one address are in flight on this core.
    std::unordered_map<uint64_t, PendingTxn> pending_requests;

    /// Submit one request to the engine and record it. Returns the msg_id.
    uint64_t submit(PacketPtr pkt, octopus::RequestType type, int connection_id,
                    int port_id);

    /**
     * Commit hook, called synchronously by this cache's L1 controller at the
     * point where the request is serialised against every other access to
     * the line. The data operation happens here, so data order equals
     * Octopus's coherence order by construction; the later completion
     * callback only sends the response.
     */
    void onCommit(uint64_t msg_id, uint64_t address);

    /**
     * Same-core ordering. O3 forwards store data only from stores it has not
     * yet sent (LSQUnit::read scans down to storeWBIt); once a store is sent,
     * a later access to the same bytes is expected to see it through the
     * memory system. Octopus does not give that: a Load hits on the S copy in
     * every SM_* state, i.e. while an older Store is still waiting for its
     * upgrade. So a data operation is held back while an older, overlapping
     * write accepted on this bridge has not performed its own.
     */
    bool blockedByOlderWrite(const PendingTxn &txn) const;
    void performData(uint64_t msg_id);
    void releaseHeldBack();
    void respond(uint64_t msg_id);

    /**
     * Invalidation hook: this core's L1 lost its readable copy of a line.
     * Nothing else in this topology emits a packet with IsInvalidate, so
     * without this LSQ::recvTimingSnoopReq() never runs: exclusive monitors
     * are only cleared through AbstractMemory, and loads that executed
     * speculatively are never replayed when another core overwrites what they
     * read. A classic cache forwards its ReadExReq/UpgradeReq upward as a
     * timing snoop; RubyPort::ruby_eviction_callback() does what this does.
     */
    void onInvalidate(uint64_t address);
    void deliverInvalidate(Addr blk_addr);
    uint64_t snoopsSent = 0;

    /// Traffic Octopus must not model as cache lines (devices, uncacheable
    /// requests): passed straight down and answered at once.
    void bypass(PacketPtr pkt, int port_id);

    /**
     * Mirror of a classic cache's blocked CPU port. The port is refused while
     * the Octopus L1 reports that it could not admit one more demand request
     * (MSHRs, demand queue or write-back buffer full, counting requests
     * already in transit toward it), and a retry is sent once it can.
     */
    bool portBlocked() const;
    void retryBlockedPorts();
    static bool anyPortNeedsRetry();

  protected:

    void tick();

  public:
    /** constructor
     */

    Octopus(const OctopusParams &params);
    // System *system;
    /**
     * Get a port with a given name and index. This is used at
     * binding time and returns a reference to a protocol-agnostic
     * port.
     *
     * @param if_name Port name
     * @param idx Index in the case of a VectorPort
     *
     * @return A reference to the given port
     */
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    void startup() override;

    void setOctLoggerEn(bool enable);
  };

} // namespace gem5

#endif // __OCTOPUS_HH__
