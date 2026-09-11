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

namespace ns3
{
  class CacheSim
  {
  public:
    CacheSim(const char *config_file_path, const char *output_logs_path);

    void run();
    void join();
    void step();
  };

  template <typename Param1T, typename Param2T, typename Param3T, typename Param4T>
  class CallbackGeneral
  {
  public:
    virtual void operator()(Param1T, Param2T, Param3T, Param4T) = 0;
  };

  template <typename ClassT, typename Param1T, typename Param2T, typename Param3T, typename Param4T>
  class CPUCallback : public CallbackGeneral<Param1T, Param2T, Param3T, Param4T>
  {
  private:
    typedef void (ClassT::*PtrMember)(Param1T, Param2T, Param3T, Param4T);
    ClassT *const object;
    const PtrMember member;

  public:
    CPUCallback(ClassT *const object, PtrMember member) : object(object), member(member)
    {
    }

    CPUCallback(const CPUCallback<ClassT, Param1T, Param2T, Param3T, Param4T> &e) : object(e.object), member(e.member)
    {
    }

    void operator()(Param1T param1, Param2T param2, Param3T param3, Param4T param4)
    {
      return (const_cast<ClassT *>(object)->*member)(param1, param2, param3, param4);
    }
  };

  template <typename ClassT, typename Param1T, typename Param2T, typename Param3T, typename Param4T>
  class MemCallback : public CallbackGeneral<Param1T, Param2T, Param3T, Param4T>
  {
  private:
    typedef void (ClassT::*PtrMember)(Param1T, Param2T, Param3T, Param4T);
    ClassT *const object;
    const PtrMember member;

  public:
    MemCallback(ClassT *const object, PtrMember member) : object(object), member(member)
    {
    }

    MemCallback(const MemCallback<ClassT, Param1T, Param2T, Param3T, Param4T> &e) : object(e.object), member(e.member)
    {
    }

    void operator()(Param1T param1, Param2T param2, Param3T param3, Param4T param4)
    {
      return (const_cast<ClassT *>(object)->*member)(param1, param2, param3, param4);
    }
  };

  enum RequestType
  {
    READ = 0,
    WRITE = 1,
    SETUP_WRITE = 2,
    SETUP_READ = 3
  };

  class ExternalCPU
  {
  public:
    void registerCPUCallback(CallbackGeneral<uint64_t, uint64_t, RequestType, uint8_t*> *cpu_callback);
    void addRequest(uint64_t address, RequestType type, uint8_t* data, int size);
    void setLoggerEnable(bool enable);

    static map<int, ExternalCPU *> *getExtCPUs();
  };

  class ExternalMem
  {
  public:
    void registerMemCallback(int, CallbackGeneral<uint64_t, uint64_t, RequestType, uint8_t*>* mem_callback);

    void read_callback(uint64_t, uint64_t);
    void write_callback(uint64_t, uint64_t);

    static ExternalMem* getExtMem();
  };

}

namespace gem5
{
  class Octopus : public ClockedObject
  {
  public:
    System *system;

  private:
    static ns3::CacheSim *cache_sim;
    static bool ext_cache_started;
    static uint64_t num_pending_req;

    /// All instantiated Octopus caches (one per cache of the external
    /// cache simulator). Used to look up the other cores when a store
    /// has to be broadcast as an invalidation.
    static std::vector<Octopus *> instances;

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
         * Send an invalidation snoop packet to the CPU. Snoop requests
         * bypass the transmit list and always succeed.
         *
         * @param packet to send.
         */
        void sendSnoop(PacketPtr pkt);

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

    void cacheSimCallback(uint64_t address, uint64_t cycle, ns3::RequestType type, uint8_t* data);

    void cacheSimMemCallback(uint64_t address, uint64_t cycle, ns3::RequestType type, uint8_t* data);

    /**
     * Broadcast an invalidation for the cache line of a just-performed
     * store to the CPUs of all other cores. The external cache simulator
     * already invalidated their cached copies of the line, but that is
     * invisible to gem5: without this snoop a core that speculatively
     * loaded the line (not yet retired) would keep the stale value and
     * retire the load, violating per-address ordering with respect to
     * the retired store. The snoop makes the O3 load queue squash those
     * loads so they are re-executed and observe the new value.
     */
    void sendInvalidations(Addr addr);

    /// The block size for the cache
    const unsigned blockSize;

    const unsigned reqFIFOSize;

    unsigned outstandingReqs;

    /// Instantiation of the CPU-side port
    std::vector<CPUSidePort> cpuPorts;

    /// Instantiation of the memory-side port
    MemSidePort memPort;

    // pkt, connection_id, port_id
    std::vector<std::pair<PacketPtr, std::pair<int,int>>> pending_requests;

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
