/*
 * File  :      MainMemoryController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 23, 2022
 */

#ifndef _MainMemoryController_H
#define _MainMemoryController_H

#include "ClockManager.h"
#include "CommunicationInterface.h"
#include "MCoreSimProjectXml.h"
#include "FRFCFS_Buffer.h"

namespace ns3
{
    class MainMemoryController : public ClockedObj
    {
    protected:
        int m_id;
        int m_llc_id;

        double m_dt;
        double m_clk_skew;
        uint64_t m_clk_cycle;

        uint32_t m_memory_latency;
        
        uint64_t m_read_count;
        uint64_t m_write_count;

        CommunicationInterface *m_lower_interface; // A pointer to the lower Interface FIFO

        FRFCFS_Buffer<Message, MainMemoryController> *m_processing_queue;

        virtual void cycleProcess();
        virtual void processLogic();
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, MainMemoryController> &buf);

    public:
        MainMemoryController(MCoreSimProjectXml &projectXml, CommunicationInterface *lower_interface, int llc_id);
        ~MainMemoryController();

        virtual void init();

        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State);
    };
}

#endif /* _MainMemoryController_H */
