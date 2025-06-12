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

#include "cpu/thread_context.hh" //for reading rio flag

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

  /**
   * A very simple cache object. Has a fully-associative data store with random
   * replacement.
   * This cache is fully blocking (not non-blocking). Only a single request can
   * be outstanding at a time.
   * This cache is a writeback cache.
   */
  class Octopus : public ClockedObject
  {
  public:
    System *system;

    // Structure to hold delayed write operations with a function pointer
    struct DelayedWrite {
      PacketPtr pkt;
      int connection_id;
      int port_id;
      
      // Add a function pointer to ensure the right object handles the delayed write
      function<void(PacketPtr, int, int)> func_ptr;
      
      DelayedWrite(PacketPtr p, int conn, int port, 
                   function<void(PacketPtr, int, int)> fp) 
          : pkt(p), connection_id(conn), port_id(port), func_ptr(fp) {}
    };

    /**
     * Locked address class that represents a physical address and a
     * context id.
     */
    class LockedAddr
    {

      private:

        // on alpha, minimum LL/SC granularity is 16 bytes, so lower
        // bits need to masked off.
        static const Addr Addr_Mask = 0xf;

      public:

        // locked address
        Addr addr;

        // locking hw context
        const ContextID contextId;

        // Indicate that there is a SC currently in fly in Octopus.
        // When a SC is in fly, the record will not be clear,
        // and this bool will be False to indicate that all LL or SC to the same context will be ignored. 
        bool writePending; 

        bool lockBySC;

        // Store delayed writes for this locked address
        std::vector<DelayedWrite> delayedWrites;

        static Addr mask(Addr paddr) { return (paddr & ~Addr_Mask); }

        // check for matching execution context
        bool matchesContext(const RequestPtr &req) const
        {
            assert(contextId != InvalidContextID);
            assert(req->hasContextId());
            return (contextId == req->contextId());
        }

        LockedAddr(const RequestPtr &req) : addr(mask(req->getPaddr())),
                                            contextId(req->contextId()),
                                            writePending(false), lockBySC(false)
        {}

        LockedAddr(const RequestPtr &req, const bool writePendingFlag) : addr(mask(req->getPaddr())),
                                            contextId(req->contextId()),
                                            writePending(writePendingFlag), lockBySC(false)
        {}

        // constructor for unserialization use
        LockedAddr(Addr _addr, int _cid) : addr(_addr), contextId(_cid), writePending(false), lockBySC(false)
        {}
    };

  private:
    static ns3::CacheSim *cache_sim;
    static bool ext_cache_started;
    static bool prev_roi_flag;
    //static int connection_id;

    struct LockEntry
    {
      PacketPtr pkt;
      int connection_id;
      int port_id;

      function<void(PacketPtr, int, int)> func_ptr;

    };

    /** List of thread contexts that have performed a load-locked (LL)
     * on the block since the last store. */

    //key is the address of a cacheline and data is a pair of the connection id (core id)
    //and a vector of all the access pending on the lock
    static map<uint64_t, pair<uint64_t, vector<LockEntry>*>> atomicLockMap;
    static std::list<LockedAddr> lockedAddrList;
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
              queue(*owner, *this, true, _label),
              needRetry(false),blockedPacket(nullptr) 
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

        /**
         * Send a retry to the peer port only if it is needed. This is called
         * from the Octopus whenever it is unblocked.
         */
        void trySendRetry();


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

        /**
         * Called by the request port if sendTimingResp was called on this
         * response port (causing recvTimingResp to be called on the request
         * port) and was unsuccessful.
         */
        //void recvRespRetry() override;

      private:
        /// Since this is a vector port, need to know what number this one is
        int connection_id;
        int id;
        
        /// The object that owns this object (Octopus)
        Octopus *owner;

        /** A normal packet queue used to store responses. */
        RespPacketQueue queue;

        /// True if the port needs to send a retry req.
        bool needRetry;

        /// If we tried to send a packet and it was blocked, store it here
        PacketPtr blockedPacket;
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
     * Send the packet to the CPU side.
     * This function assumes the pkt is already a response packet and forwards
     * it to the correct port. This function also unblocks this object and
     * cleans up the whole request.
     *
     * @param the packet to send to the cpu side
     */
    void sendResponse(PacketPtr pkt, int port_id = 0);

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
    void handleAtomic(PacketPtr pkt, int connection_id, int port_id);

    /**
     * Access the cache for a timing access. This is called after the cache
     * access latency has already elapsed.
     */
    void accessTiming(PacketPtr pkt, int connection_id, int port_id = 0);

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

    void cacheSimCallback(uint64_t address, uint64_t cycle, ns3::RequestType type, uint8_t* data);

    void cacheSimMemCallback(uint64_t address, uint64_t cycle, ns3::RequestType type, uint8_t* data);

    /// The block size for the cache
    const unsigned blockSize;

    const unsigned reqFIFOSize;

    unsigned outstandingReqs;

    /// Instantiation of the CPU-side port
    std::vector<CPUSidePort> cpuPorts;

    /// Instantiation of the memory-side port
    MemSidePort memPort;

    // std::map<Addr, std::pair<PacketPtr, int>> pending_requests;
    std::vector<std::pair<PacketPtr, int>> pending_requests;

    bool handleSwap(PacketPtr pkt);

    // helper function for checkLockedAddrs(): we really want to
    // inline a quick check for an empty locked addr list (hopefully
    // the common case), and do the full list search (if necessary) in
    // this out-of-line function
    bool checkLockedAddrList(PacketPtr pkt, bool doErase = false, int connection_id = -1, int port_id = -1);

    // Record the address of a load-locked operation so that we can
    // clear the execution context's lock flag if a matching store is
    // performed
    void trackLoadLocked(PacketPtr pkt);

    // Compare a store address with any locked addresses so we can
    // clear the lock flag appropriately.  Return value set to 'false'
    // if store operation should be suppressed (because it was a
    // conditional store and the address was no longer locked by the
    // requesting execution context), 'true' otherwise.  Note that
    // this method must be called on *all* stores since even
    // non-conditional stores must clear any matching lock addresses.
    bool
    writeOK(PacketPtr pkt, bool doErase = false, int connection_id = -1, int port_id = -1);
  protected:

    void tick();

    // Map to store original data for swap operations
    std::map<Addr, std::pair<uint8_t*, int>> swapDataMap;

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
