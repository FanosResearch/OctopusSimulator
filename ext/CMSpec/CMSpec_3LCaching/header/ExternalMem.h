/*
 * File  :      ExternalMem.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sept 6, 2022
 */

#ifndef _ExternalMem_H
#define _ExternalMem_H

#include "ClockManager.h"
#include "CommunicationInterface.h"
#include "CacheXml.h"
#include "FRFCFS_Buffer.h"
#include "IdGenerator.h"
#include "CPU.h"
#include "Initializable.h"
#include "MCoreSimProjectXml.h"
#include "ExternalCPU.h"

#include <map>

namespace ns3
{
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

    class ExternalMem : public ClockedObj 
    {
    private:
        static ExternalMem* ext_Mem;

    protected:
        int m_id;
        int m_llc_id;
        int m_llc_line_size;

        double m_dt;
        double m_clk_skew;
        uint64_t m_clk_cycle;
        uint64_t m_read_count;
        uint64_t m_write_count;

        CommunicationInterface *m_lower_interface; // A pointer to the upper Interface FIFO

        FRFCFS_Buffer<Message, ExternalMem> *m_processing_queue;

        vector<Message> m_pending_requests;
        vector<Message> m_output_buffer;
        
        map<int,CallbackGeneral<uint64_t, uint64_t, RequestType, uint8_t*>*> m_mem_callback; //Address, Clock Cycle, Type
        
        Initializable *m_memory_component;

        virtual void cycleProcess();
        virtual void processLogic();
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, ExternalMem> &buf);

    public:
        ExternalMem(MCoreSimProjectXml &xml,CommunicationInterface *lower_interface, int llc_id);
        ~ExternalMem();

        virtual void init();

        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State);

        void registerMemCallback(int,CallbackGeneral<uint64_t, uint64_t, RequestType, uint8_t*>* mem_callback);

        void read_callback(uint64_t, uint64_t);
        void write_callback(uint64_t, uint64_t);

        static ExternalMem* getExtMem();
        static void setExtMem(ExternalMem*);
    };
}

#endif /* _ExternalMem_H */
