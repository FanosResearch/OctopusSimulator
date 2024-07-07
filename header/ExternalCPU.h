/*
 * File  :      ExternalCPU.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sept 6, 2022
 */

#ifndef _ExternalCPU_H
#define _ExternalCPU_H

#include "ClockManager.h"
#include "CommunicationInterface.h"
#include "CacheXml.h"
#include "FRFCFS_Buffer.h"
#include "IdGenerator.h"
#include "CPU.h"
#include "Initializable.h"

#include <map>

namespace octopus
{
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

    class ExternalCPU : public ClockedObj
    {
    private:
        static map<int, ExternalCPU*> ext_CPUs;

    protected:
        int m_id;

        uint64_t m_clk_cycle;

        CommunicationInterface *m_upper_interface; // A pointer to the upper Interface FIFO

        FRFCFS_Buffer<Message, ExternalCPU> *m_processing_queue;
        
        CallbackGeneral<uint64_t, uint64_t, RequestType, uint8_t*>* m_cpu_callback; //Address, Clock Cycle, Type
        
        Initializable *m_memory_component;

        virtual void cycleProcess();
        virtual void processLogic();
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, ExternalCPU> &buf);

    public:
        ExternalCPU(CacheXml &xml, CommunicationInterface *upper_interface);
        ~ExternalCPU();

        virtual void init();

        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State);

        void registerCPUCallback(CallbackGeneral<uint64_t, uint64_t, RequestType, uint8_t*>* cpu_callback);
        void registerInitializableMemory(Initializable *component);

        void addRequest(uint64_t address, RequestType type, uint8_t* data = NULL, int size = 64);

        static map<int, ExternalCPU*>* getExtCPUs();
    };
}

#endif /* _ExternalCPU_H */
