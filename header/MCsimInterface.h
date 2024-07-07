/*
 * File  :      MCsimInterface.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On August 4, 2022
 */

#ifndef _MCsimInterface_H
#define _MCsimInterface_H

#include "ClockManager.h"
#include "CommunicationInterface.h"
#include "MCoreSimProjectXml.h"
#include "FRFCFS_Buffer.h"

#include "MCsim.h"

namespace octopus
{
    class MCsimInterface : public ClockedObj
    {
    protected:
        int m_id;
        int m_llc_id;
        int m_llc_line_size;
        
        uint64_t m_clk_cycle;
        uint64_t m_read_count;
        uint64_t m_write_count;

        vector<Message> m_pending_requests;
        vector<Message> m_output_buffer;
        
        CommunicationInterface *m_lower_interface; // A pointer to the lower Interface FIFO

        FRFCFS_Buffer<Message, MCsimInterface> *m_processing_queue;
        
        MCsim::MultiChannelMemorySystem *m_mcsim;

        virtual void cycleProcess();
        virtual void processLogic();
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, MCsimInterface> &buf);

        virtual void read_callback(unsigned, uint64_t, uint64_t);
        virtual void write_callback(unsigned, uint64_t, uint64_t);

    public:
        MCsimInterface(MCoreSimProjectXml &projectXml, CommunicationInterface *lower_interface, int llc_id);
        ~MCsimInterface();

        virtual void init();

        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State);
    };
}

#endif /* _MCsimInterface_H */
