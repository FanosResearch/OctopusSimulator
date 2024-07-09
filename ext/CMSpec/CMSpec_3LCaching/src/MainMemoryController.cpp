/*
 * File  :      MainMemoryController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 23, 2022
 */

#include "../header/MainMemoryController.h"

namespace ns3
{
    // private controller constructor
    MainMemoryController::MainMemoryController(MCoreSimProjectXml &projectXml, CommunicationInterface *lower_interface, int llc_id) : ClockedObj(projectXml.GetDRAMCtrlClkNanoSec())
    {
        m_id = projectXml.GetDRAMId();
        m_llc_id = llc_id;

        m_dt = projectXml.GetDRAMCtrlClkNanoSec();
        m_clk_skew = projectXml.GetDRAMCtrlClkSkew();
        m_clk_cycle = 1;

        m_memory_latency = projectXml.GetDRAMFixedLatcy();
        
        m_read_count = 0;
        m_write_count = 0;

        m_lower_interface = lower_interface;    

        
        m_processing_queue = new FRFCFS_Buffer<Message, MainMemoryController>(&MainMemoryController::getRequestState, this);
    }

    MainMemoryController::~MainMemoryController()
    {
    }

    void MainMemoryController::cycleProcess()
    {
        processLogic();
        m_clk_cycle++;
    }

    void MainMemoryController::init()
    {
    }

    void MainMemoryController::processLogic()
    {
        addRequests2ProcessingQueue(*m_processing_queue);

        Message ready_msg;
        if (m_processing_queue->getFirstReady(&ready_msg) == false)
            return;

        if (ready_msg.data == NULL) //Read message 
        {
            m_read_count++;
            // uint64_t data = m_read_count;
            uint8_t return_data[64] = {0};

            Message msg = Message(ready_msg.msg_id,    // Id
                                  ready_msg.addr,      // Addr
                                  m_clk_cycle,         // Cycle
                                  0,                   // Complementary_value
                                  ready_msg.owner);    // Owner
            msg.to.push_back((uint16_t) m_llc_id);     // To
            // msg.copy((uint8_t*)&data);
            msg.copy(return_data);
                    
            if (!m_lower_interface->pushMessage(msg, m_clk_cycle, MessageType::DATA_RESPONSE))
            {
                cout << "MainMemoryController(id = " << this->m_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
                exit(0);
            }
        }
        else 
        {
            m_write_count++;
            // cout << "MainMemoryController: write msg id = " << ready_msg.msg_id;
            // cout << ", count is " << m_write_count;
            // cout << " and clk is " << m_clk_cycle << endl;
        }
    }
    
    void MainMemoryController::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, MainMemoryController> &buf)
    {
        Message msg;

        if (m_lower_interface->peekMessage(&msg))
        {
            msg.source = Message::Source::LOWER_INTERCONNECT;
            msg.cycle = m_clk_cycle;
            if (buf.pushBack(msg, FRFCFS_State::NonReady))
                m_lower_interface->popFrontMessage();
        }
    }

    FRFCFS_State MainMemoryController::getRequestState(const Message &msg, FRFCFS_State current_state)
    {
        if(current_state == FRFCFS_State::NonReady)
        {
            if(msg.cycle + m_memory_latency <= m_clk_cycle)
                return FRFCFS_State::Ready;
        }

        return FRFCFS_State::NonReady;
    }    
}