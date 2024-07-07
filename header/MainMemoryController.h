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
#include "Configurable.h"
#include "DebugPrint.h"
#include "CommunicationInterface.h"
#include "MCoreSimProjectXml.h"
#include "FRFCFS_Buffer.h"

namespace octopus
{
    class MainMemoryController : public ClockedObj, public Configurable
    {
    protected:
        int m_id;
        int m_llc_id;

        uint64_t m_clk_cycle;

        uint32_t m_memory_latency;
        
        uint64_t m_read_count;
        uint64_t m_write_count;

        CommunicationInterface *m_lower_interface; // A pointer to the lower Interface FIFO
        DebugPrint* dprint;

        FRFCFS_Buffer<Message, MainMemoryController> *m_processing_queue;

        virtual void cycleProcess();
        virtual void processLogic();
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, MainMemoryController> &buf);

    public:
        MainMemoryController(ParametersMap map, CommunicationInterface *lower_interface,
                             string pname = "",
                             string config_path = string(CONFIGURATION_PATH),
                             string name = STRINGIFY(MainMemoryController));
        ~MainMemoryController();

        virtual void init();

        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State);
    };
}

#endif /* _MainMemoryController_H */
