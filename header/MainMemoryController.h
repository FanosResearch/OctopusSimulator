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
#include "ProcessingBuffer.h"

#include "Arbiter.h"
#include "RRArbiter.h"
#include "FCFSArbiter.h"
#include "RROFArbiter.h"

#include <math.h>

namespace ns3
{
    class MainMemoryController : public ClockedObj, public Configurable
    {
    protected:
        int m_id;
        int m_llc_id;

        uint64_t m_clk_cycle;
        uint64_t m_ready_cycle;
        uint64_t m_process_end_cycle;

        uint32_t m_memory_latency;
        
        uint64_t m_read_count;
        uint64_t m_write_count;
        uint64_t m_cache_line_mask = -1;

        Message *read_write_message = NULL;

        CommunicationInterface *m_lower_interface; // A pointer to the lower Interface FIFO
        DebugPrint* dprint;

        ProcessingBuffer<Message> *m_processing_queue;

        virtual Arbiter* createArbiter(string arbiter_type, vector<int>* candidates_ids, int arbiter_period = 0);
        virtual void cycleProcess();
        virtual void processLogic();
        virtual void addRequests2ProcessingQueue(ProcessingBuffer<Message> &buf);
        virtual bool isReady();
        virtual void performRW();

    public:
        MainMemoryController(ParametersMap map, CommunicationInterface *lower_interface,
                             string pname = "",
                             string config_path = string(CONFIGURATION_PATH),
                             string name = STRINGIFY(MainMemoryController));
        ~MainMemoryController();

        virtual void init();

        virtual EntryState getRequestState(const Message &, EntryState);
        virtual void setLineMask(uint64_t line_size) { m_cache_line_mask = ~(line_size -1); }
    };
}

#endif /* _MainMemoryController_H */
