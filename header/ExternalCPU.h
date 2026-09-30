/*
 * File  :      ExternalCPU.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sept 6, 2022
 *
 * The adaptor between an external core model (the gem5 bridge) and one L1
 * controller. Requests enter through addRequest(); the L1 reports the commit
 * and the completion of each one, and the loss of any readable line, through
 * the callbacks registered here.
 */

#ifndef _ExternalCPU_H
#define _ExternalCPU_H

#include "ClockManager.h"
#include "Configurable.h"
#include "CommunicationInterface.h"
#include "FRFCFS_Buffer.h"
#include "IdGenerator.h"
#include "RequestType.h"
#include "Initializable.h"

#include <map>
#include <list>
#include <string>

namespace octopus
{
    class BaseController;

    // Bumped whenever the interface an embedder (the gem5 bridge) compiles
    // against changes. The embedder compares this constant, baked into its own
    // binary, with ExternalCPU::portVersion() from the library it loaded, so a
    // stale library or a stale embedder fails at start-up with a message
    // instead of misbehaving.
    static constexpr int kOctopusPortVersion = 5;

    // Variadic so callbacks of different arity share one base.
    template <typename... ParamsT>
    class CallbackGeneral
    {
        public:
            virtual ~CallbackGeneral() {}
            virtual void operator()(ParamsT...) = 0;
    };

    template <typename ClassT, typename... ParamsT>
    class CPUCallback : public CallbackGeneral<ParamsT...>
    {
    private:
        typedef void (ClassT::*PtrMember)(ParamsT...);
        ClassT *const object;
        const PtrMember member;

    public:
        CPUCallback(ClassT *const object, PtrMember member) : object(object), member(member)
        {
        }

        CPUCallback(const CPUCallback<ClassT, ParamsT...> &e) : object(e.object), member(e.member)
        {
        }

        void operator()(ParamsT... params)
        {
            return (const_cast<ClassT *>(object)->*member)(params...);
        }
    };

    // Completion of one CPU request: msg_id (as returned by addRequest),
    // address, clock cycle, request type, line data (may be NULL).
    typedef CallbackGeneral<uint64_t, uint64_t, uint64_t, RequestType, uint8_t*> CpuCompletionCallback;

    // Commit of one CPU request: msg_id, address. Called synchronously from the
    // L1 controller at the point where the request is serialised against every
    // other access to that line (a hit in a permitting state, or the data
    // arriving for a pending miss), i.e. before the completion above is even
    // queued. An embedder performs the data operation here so that data order
    // equals coherence order by construction.
    typedef CallbackGeneral<uint64_t, uint64_t> CpuCommitCallback;

    // This CPU's L1 lost a readable copy of a line (address): another core
    // took ownership, the LLC invalidated it, or it was replaced. Called
    // synchronously when that state change is committed.
    typedef CallbackGeneral<uint64_t> CpuInvalidateCallback;

    class ExternalCPU : public ClockedObj, public Configurable
    {
    private:
        static std::map<int, ExternalCPU*> ext_CPUs;

    protected:
        int m_id;
        uint64_t m_clk_cycle;
        uint64_t m_link_full_holds = 0;

        CommunicationInterface *m_upper_interface; // A pointer to the upper Interface FIFO
        FRFCFS_Buffer<Message, ExternalCPU> *m_processing_queue;
        BaseController *m_cache = NULL;             // the L1 this CPU feeds

        CpuCompletionCallback *m_cpu_callback = NULL;
        CpuCommitCallback *m_commit_callback = NULL;
        CpuInvalidateCallback *m_invalidate_callback = NULL;
        Initializable *m_memory_component = NULL;

        virtual void cycleProcess();
        virtual void processLogic();
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, ExternalCPU> &buf);

    public:
        // config_path empty means the library's own configuration directory,
        // so an embedder need not know the compile-time path macro.
        ExternalCPU(ParametersMap map, int id, CommunicationInterface *upper_interface,
                    std::string pname = "", std::string config_path = "",
                    std::string name = STRINGIFY(ExternalCPU));
        ~ExternalCPU();

        virtual void init();
        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State);

        static int portVersion();
        int getId() const { return m_id; }

        void registerCPUCallback(CpuCompletionCallback* cpu_callback);
        void registerCommitCallback(CpuCommitCallback* commit_callback);
        void registerInvalidateCallback(CpuInvalidateCallback* invalidate_callback);
        void registerInitializableMemory(Initializable *component);

        // Returns the msg_id that the commit and completion callbacks will
        // carry for this request (0 for the SETUP_* pseudo-requests, which
        // never complete).
        uint64_t addRequest(uint64_t address, RequestType type, uint8_t* data = NULL, int size = 64);

        // Called by this CPU's L1 controller; no-ops when no embedder has
        // registered (the standalone simulator).
        void commit(uint64_t msg_id, uint64_t address);
        void invalidate(uint64_t address);

        // The L1 this adaptor feeds, for isBlocked().
        void attachCache(BaseController *cache) { m_cache = cache; }

        // Mirrors a classic cache's blocked CPU port. `outstanding` is the
        // number of this core's requests the embedder has accepted and not
        // yet answered (squashed ones included, until their response comes
        // back). Blocked when that reaches the L1's queue bound, which is the
        // total the core is credited with, hits and waiting followers
        // included, or when the L1's MSHR or write-back buffer is full. The
        // embedder refuses its port while this holds and retries once it
        // clears; every FIFO on the way is then bounded by the same number.
        bool isBlocked(int outstanding) const;

        // The credit count above (the L1's processing_queue_size), -1 if
        // unbounded.
        int outstandingLimit() const;

        static std::map<int, ExternalCPU*>* getExtCPUs();
    };
}

#endif /* _ExternalCPU_H */
