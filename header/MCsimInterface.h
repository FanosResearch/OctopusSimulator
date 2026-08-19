/*
 * File  :      MCsimInterface.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On August 4, 2022
 *
 * Octopus adapter for the MCsim DRAM simulator (vendored from grrof under
 * src/MCsim/). Drop-in alternative to MainMemoryController: sits on the LLC<->DRAM
 * bus, forwards LLC read/write requests into MCsim, and returns read data when
 * MCsim's callback fires. Selected via `main_memory_type=MCsim` in the system config.
 */

#ifndef _MCsimInterface_H
#define _MCsimInterface_H

#include "ClockManager.h"
#include "CommunicationInterface.h"
#include "FRFCFS_Buffer.h"
#include "MCsim/MCsim.h" // the public facade (header/MCsim/), NOT the library-internal src/MCsim/src/MCsim.h

#include <string>
#include <vector>

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

        std::vector<Message> m_pending_requests;
        std::vector<Message> m_output_buffer;

        CommunicationInterface *m_lower_interface; // toward the LLC (bus to DRAM)

        FRFCFS_Buffer<Message, MCsimInterface> *m_processing_queue;

        MCsim::MultiChannelMemorySystem *m_mcsim;

        virtual void cycleProcess();
        virtual void processLogic();
        virtual void addRequests2ProcessingQueue(FRFCFS_Buffer<Message, MCsimInterface> &buf);

        virtual void read_callback(unsigned, uint64_t, uint64_t);
        virtual void write_callback(unsigned, uint64_t, uint64_t);

    public:
        // mem_system names the scheduler config dir under src/MCsim/system/<name>/<name>.ini
        // (e.g. "FRFCFS"). Device is configured for DDR4-2400U, 8Gb x8.
        MCsimInterface(CommunicationInterface *lower_interface, int dram_id, int llc_id,
                       int num_cores, int block_size, const std::string &mem_system = "FRFCFS");
        ~MCsimInterface();

        virtual void init();

        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State);
    };
}

#endif /* _MCsimInterface_H */
