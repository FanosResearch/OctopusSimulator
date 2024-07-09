/*
 * File  :      ExternalMem.cpp
 * Author:      Yuying Lai
 * Email :      Laiy24@mcmaster.ca
 *
 * Created On Mar 5, 2023
 */

#include "../header/ExternalMem.h"

namespace ns3
{
    // private controller constructor
    ExternalMem::ExternalMem(MCoreSimProjectXml &xml, CommunicationInterface *lower_interface, int llc_id) : ClockedObj(xml.GetDRAMCtrlClkNanoSec())
    {
        m_id = xml.GetDRAMId();
        m_llc_id = llc_id;

        m_dt = xml.GetDRAMCtrlClkNanoSec();
        m_clk_skew = xml.GetDRAMCtrlClkSkew();
        m_clk_cycle = 1;

        m_lower_interface = lower_interface;

        m_processing_queue = new FRFCFS_Buffer<Message, ExternalMem>(&ExternalMem::getRequestState, this);

        m_memory_component = NULL;

        m_llc_line_size = xml.GetSharedCache().GetBlockSize();

    }

    ExternalMem::~ExternalMem()
    {
    }

    void ExternalMem::cycleProcess()
    {
        processLogic();
        m_clk_cycle++;
    }

    void ExternalMem::init()
    {
    }

    void ExternalMem::processLogic()
    {
        addRequests2ProcessingQueue(*m_processing_queue);

        Message ready_msg;
        if (m_processing_queue->getFirstReady(&ready_msg))
        {
            int core_id = ready_msg.owner;
            if(m_mem_callback.at(core_id) != NULL)
            {
                //if (m_mcsim->addRequest(ready_msg.owner, ready_msg.addr, ready_msg.data == NULL, m_llc_line_size)) // 1 -> Read, 0 -> Write
                (*(m_mem_callback.at(core_id)))(ready_msg.addr, this->m_clk_cycle, (RequestType)ready_msg.complementary_value, ready_msg.data);
                if(ready_msg.data == NULL) //Add read requests only
                    {
                    m_pending_requests.push_back(ready_msg);
                    }
            }
            else
            {
                cout << "ExternalMem(id = " << this->m_id << "): ";
                cout << "Error Mem Callback is invalid" << endl;
                exit(0);
            }
        }

        if(!m_output_buffer.empty())
        {
            if (!m_lower_interface->pushMessage(m_output_buffer[0], m_clk_cycle, MessageType::DATA_RESPONSE))
            {
                cout << "MCsimInterface(id = " << this->m_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
                exit(0);
            }
            m_output_buffer.erase(m_output_buffer.begin());
        }
    }

    void ExternalMem::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, ExternalMem> &buf)
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

    FRFCFS_State ExternalMem::getRequestState(const Message &msg, FRFCFS_State current_state)
    {
        return FRFCFS_State::Ready;
    }    

    void ExternalMem::registerMemCallback(int id,CallbackGeneral<uint64_t, uint64_t, RequestType, uint8_t*>* mem_callback)
    {
        this->m_mem_callback.emplace(id,mem_callback);
    }

    void ExternalMem::read_callback(uint64_t address, uint64_t clock_cycle)
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
                msg.to.push_back((uint16_t)m_llc_id);               // To
                msg.copy((uint8_t *)&data);
                m_output_buffer.push_back(msg);

                m_pending_requests.erase(m_pending_requests.begin() + i);
                found = true;
                break;
            }
        }
        
        if(!found)
        {
            cout << "MCsimInterface: Error read_callback couldn't find the pending request" << endl;
            exit(0);
        }
    }

    void ExternalMem::write_callback(uint64_t address, uint64_t clock_cycle)
    {
        m_write_count++;
    }

    ExternalMem* ExternalMem::ext_Mem;
    
    ExternalMem* ExternalMem::getExtMem()
    {
        return ExternalMem::ext_Mem;
    }

    void ExternalMem::setExtMem(ExternalMem* mem)
    {
        ext_Mem = mem;
   
    }
}