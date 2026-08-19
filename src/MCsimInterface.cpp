/*
 * File  :      MCsimInterface.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On August 4, 2022
 */

#include "../header/MCsimInterface.h"

using namespace std;

namespace octopus
{
    MCsimInterface::MCsimInterface(CommunicationInterface *lower_interface, int dram_id, int llc_id,
                                   int num_cores, int block_size, const std::string &mem_system)
        : ClockedObj(1) // period MUST be non-zero: ClockManager reschedules at current_time+period,
                        // so period 0 self-reschedules at the same timestamp forever and starves the
                        // whole clock (CPUs never advance). Period 1 = tick every cycle to drive update().
    {
        m_id = dram_id;
        m_llc_id = llc_id;
        m_clk_cycle = 1;
        m_llc_line_size = block_size;
        m_read_count = 0;
        m_write_count = 0;

        m_lower_interface = lower_interface;
        m_processing_queue = new FRFCFS_Buffer<Message, MCsimInterface>(&MCsimInterface::getRequestState, this);

        /******************** Initialization of MCsim (DDR4-2400U, 8Gb x8, 1 channel/1 rank) ********************/
        std::string sys_init_file = std::string(MCSIM_PATH) + "system/" + mem_system + "/" + mem_system + ".ini";
        m_mcsim = MCsim::getMemorySystemInstance(
            num_cores,
            sys_init_file,
            "DDR4",
            "2400U",
            "8Gb_x8",
            1,
            1);
        m_mcsim->setCPUClockSpeed(2.4 * 1e9);

        MCsim::TransactionCompleteCB *read_cb = new MCsim::MCsimCallback<MCsimInterface, void, unsigned, uint64_t, uint64_t>(this, &MCsimInterface::read_callback);
        MCsim::TransactionCompleteCB *write_cb = new MCsim::MCsimCallback<MCsimInterface, void, unsigned, uint64_t, uint64_t>(this, &MCsimInterface::write_callback);
        m_mcsim->RegisterCallbacks(read_cb, write_cb);
    }

    MCsimInterface::~MCsimInterface()
    {
        delete m_mcsim;
    }

    void MCsimInterface::cycleProcess()
    {
        processLogic();
        m_mcsim->update();

        m_clk_cycle++;
    }

    void MCsimInterface::init()
    {
    }

    void MCsimInterface::processLogic()
    {
        addRequests2ProcessingQueue(*m_processing_queue);

        Message ready_msg;
        if (m_processing_queue->getFirstReady(&ready_msg))
        {
            if (m_mcsim->addRequest(ready_msg.owner, ready_msg.addr, ready_msg.data == NULL, m_llc_line_size)) // 1 -> Read, 0 -> Write
            {
                if (ready_msg.data == NULL) // Add read requests only (writes are fire-and-forget)
                    m_pending_requests.push_back(ready_msg);
            }
            else
            {
                cout << "MCsimInterface: Error failed to add request to MCsim" << endl;
                exit(0);
            }
        }

        if (!m_output_buffer.empty())
        {
            if (!m_lower_interface->pushMessage(m_output_buffer[0], m_clk_cycle, MessageType::DATA_RESPONSE))
            {
                cout << "MCsimInterface(id = " << this->m_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
                exit(0);
            }
            m_output_buffer.erase(m_output_buffer.begin());
        }
    }

    void MCsimInterface::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, MCsimInterface> &buf)
    {
        Message msg;

        if (m_lower_interface->peekMessage(&msg))
        {
            msg.source = Message::Source::LOWER_INTERCONNECT;
            msg.cycle = m_clk_cycle;
            if (buf.pushBack(msg, FRFCFS_State::Ready))
                m_lower_interface->popFrontMessage();
        }
    }

    FRFCFS_State MCsimInterface::getRequestState(const Message &msg, FRFCFS_State current_state)
    {
        return FRFCFS_State::Ready;
    }

    void MCsimInterface::read_callback(unsigned id, uint64_t address, uint64_t clock_cycle)
    {
        bool found = false;
        for (int i = 0; i < (int)m_pending_requests.size(); i++)
        {
            if (m_pending_requests[i].addr == address)
            {
                uint64_t data = m_read_count;
                m_read_count++;

                Message msg = Message(m_pending_requests[i].msg_id, // Id
                                      m_pending_requests[i].addr,   // Addr
                                      m_clk_cycle,                  // Cycle
                                      0,                            // Complementary_value
                                      m_pending_requests[i].owner); // Owner
                msg.to.push_back((uint16_t)m_llc_id);               // To (back to the LLC)
                msg.copy((uint8_t *)&data);
                m_output_buffer.push_back(msg);

                m_pending_requests.erase(m_pending_requests.begin() + i);
                found = true;
                break;
            }
        }

        if (!found)
        {
            cout << "MCsimInterface: Error read_callback couldn't find the pending request" << endl;
            exit(0);
        }
    }

    void MCsimInterface::write_callback(unsigned id, uint64_t address, uint64_t clock_cycle)
    {
        m_write_count++;
    }
}
