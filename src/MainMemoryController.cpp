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
    MainMemoryController::MainMemoryController(ParametersMap map, CommunicationInterface *lower_interface,
        string pname, string config_path, string name) : ClockedObj(0), Configurable(map, config_path, name, pname)
    {
        //Parameters initialization
        m_clk_period = std::get<int>(parameters.at(STRINGIFY(m_clk_period)).value);
        m_id = std::get<int>(parameters.at(STRINGIFY(m_id)).value);
        m_llc_id = std::get<int>(parameters.at(STRINGIFY(m_llc_id)).value);
        m_memory_latency = std::get<int>(parameters.at(STRINGIFY(m_memory_latency)).value);

        string processing_q_arbiter_t = std::get<string>(parameters.at(STRINGIFY(processing_q_arbiter_t)).value);
        vector<int>* processing_q_arbiter_candidates_ids = NULL; //ids of the cores subjected to data access arbiteration
        
        if(processing_q_arbiter_t != STRINGIFY(NULL))
            processing_q_arbiter_candidates_ids = new vector<int>(std::get<vector<int>>(parameters.at(STRINGIFY(processing_q_arbiter_candidates_ids)).value));
        
        //Constructor
        m_clk_cycle = 1;
        m_process_end_cycle = 0;
        m_ready_cycle = m_clk_cycle + m_memory_latency;

        m_read_count = 0;
        m_write_count = 0;

        m_lower_interface = lower_interface;
        
        dprint = new DebugPrint(getSubMap(STRINGIFY(dprint)), name + std::to_string(m_id), parent_name + "." + name);
             
        m_processing_queue = new ProcessingBuffer<Message>([&] (const Message& msg, EntryState state) -> EntryState {
                                                                return this->getRequestState(msg, state);}, -1,
                                                           createArbiter(processing_q_arbiter_t, processing_q_arbiter_candidates_ids));
        
        if(processing_q_arbiter_t == STRINGIFY(RROFArbiter)) //processing queue arbiter
        {
            ((RROFArbiter*)m_processing_queue->getArbiter())->set_arbiter_name("memory_" + std::to_string(m_id), m_id);
        }
    }

    MainMemoryController::~MainMemoryController()
    {
    }

    Arbiter* MainMemoryController::createArbiter(string arbiter_type, vector<int>* candidates_ids, int arbiter_period)
    {
        if(arbiter_type == STRINGIFY(NULL))
            return NULL;
        else if(arbiter_type == STRINGIFY(RRArbiter))                                                    
            return new RRArbiter(candidates_ids, arbiter_period);
        else if(arbiter_type == STRINGIFY(FCFSArbiter))                                                    
            return new FCFSArbiter(candidates_ids, arbiter_period);
        else if(arbiter_type == STRINGIFY(RROFArbiter))                                                    
            return new RROFArbiter(candidates_ids, arbiter_period);
        else
            return NULL;
    }

    void MainMemoryController::cycleProcess()
    {
        processLogic();
        performRW();
        m_clk_cycle++;
    }

    void MainMemoryController::init()
    {
    }

    void MainMemoryController::processLogic()
    {
        addRequests2ProcessingQueue(*m_processing_queue);
        if(m_processing_queue->empty())
            m_ready_cycle = m_clk_cycle + m_memory_latency;

        Message ready_msg;
        if(!isReady())
            return;
        if (m_processing_queue->getReady(&ready_msg) == false)
            return;

        m_process_end_cycle = m_ready_cycle + m_memory_latency - 1;
        m_ready_cycle = m_clk_cycle + m_memory_latency;
        
        if(read_write_message != NULL)
        {
            cout << "MainMemoryController: read_write_message didn't finish processing\n";
            exit(0);
        }
        read_write_message = new Message(ready_msg);
    }
    
    void MainMemoryController::performRW()
    {
        if(m_clk_cycle != m_process_end_cycle)
            return;
        if(read_write_message == NULL)
            return;

        if (read_write_message->data == NULL) //Read message 
        {
            m_read_count++;
            uint8_t return_data[64] = {0};

            Message msg = Message(read_write_message->msg_id,    // Id
                                  read_write_message->addr,      // Addr
                                  m_clk_cycle,         // Cycle
                                  0,                   // Complementary_value
                                  read_write_message->owner);    // Owner
            msg.to.push_back((uint16_t) m_llc_id);     // To
            msg.copy(return_data);
            
            // RROFArbiter::incrementEraseCount(read_write_message);
            if (!m_lower_interface->pushMessage(msg, m_clk_cycle, MessageType::DATA_RESPONSE))
            {
                cout << "MainMemoryController(id = " << this->m_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
                exit(0);
            }
        }
        else 
        {
            m_write_count++;
            RROFArbiter::removeRequestFromQueues(*read_write_message);
            // cout << "MainMemoryController: write msg id = " << read_write_message->msg_id;
            // cout << ", count is " << m_write_count;
            // cout << " and clk is " << m_clk_cycle << endl;
        }

        delete read_write_message;
        read_write_message = NULL;
    }

    void MainMemoryController::addRequests2ProcessingQueue(ProcessingBuffer<Message> &buf)
    {
        Message msg;

        if (m_lower_interface->peekMessage(&msg))
        {
            msg.source = Message::Source::LOWER_INTERCONNECT;
            msg.cycle = m_clk_cycle;
            Message write_msg;
            if(buf.find(msg, &write_msg, [&](Message &T1, Message &T2) -> bool{
                return (T1.addr & this->m_cache_line_mask) == (T2.addr & this->m_cache_line_mask);
            }))
            {
                if(msg.data == NULL)
                {
                    m_read_count++;
                    Message new_msg = Message(msg.msg_id,    // Id
                                              msg.addr,      // Addr
                                              m_clk_cycle,   // Cycle
                                              0,             // Complementary_value
                                              msg.owner);    // Owner
                    new_msg.to.push_back((uint16_t) m_llc_id);

                    if(write_msg.data != NULL)
                        new_msg.copy(write_msg.data);
                    else
                    {
                        cout << "MainMemoryController(id = " << this->m_id << "): Write message without data" << endl;
                        exit(0);
                    }
                    
                    if (!m_lower_interface->pushMessage(new_msg, m_clk_cycle, MessageType::DATA_RESPONSE))
                    {
                        cout << "MainMemoryController(id = " << this->m_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
                        exit(0);
                    }

                    //Retire the write message as data is returning back to LLC
                    buf.removeItem(write_msg);
                    RROFArbiter::removeRequestFromQueues(write_msg);
                }
                else //write after a write (Write-Read-Write)
                {
                    if(write_msg.data != NULL)
                    {
                        m_write_count++;

                        buf.removeItem(write_msg);
                        RROFArbiter::removeRequestFromQueues(write_msg);

                        buf.pushBack(msg, EntryState::NonReady);
                    }
                    else//error
                    {
                        // cout << "MainMemoryController(id = " << this->m_id << "): Write message without data" << endl;
                        // exit(0);
                        //A workaround until adding RROF to the system bus 
                        //A read(what is called write_msg in this context) arrives before the write
                        if(msg.data == NULL)
                        {
                            cout << "MainMemoryController(id = " << this->m_id << "): Write message without data" << endl;
                            exit(0);
                        }
                        // buf.pushBack(msg, EntryState::NonReady); //push the write msg
                        //Retire the write message as data is returning back to LLC
                        RROFArbiter::removeRequestFromQueues(msg);
                        
                        buf.removeItem(write_msg);
                        m_read_count++;
                        Message new_msg = Message(write_msg.msg_id,    // Id
                                                  write_msg.addr,      // Addr
                                                  m_clk_cycle,         // Cycle
                                                  0,                   // Complementary_value
                                                  write_msg.owner);    // Owner
                        new_msg.to.push_back((uint16_t) m_llc_id);     // To
                        new_msg.copy(msg.data);
                        
                        if (!m_lower_interface->pushMessage(new_msg, m_clk_cycle, MessageType::DATA_RESPONSE))
                        {
                            cout << "MainMemoryController(id = " << this->m_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
                            exit(0);
                        }
                    }
                }
                m_lower_interface->popFrontMessage();
            }
            else if (buf.pushBack(msg, EntryState::NonReady))
                m_lower_interface->popFrontMessage();
        }
    }

    EntryState MainMemoryController::getRequestState(const Message &msg, EntryState current_state)
    {
        if(current_state == EntryState::NonReady)
        {
            if(isReady())
                return EntryState::Ready;
            else
                return EntryState::NonReady;
        }
        else
            return current_state;
    }

    bool MainMemoryController::isReady()
    {
        return m_ready_cycle <= m_clk_cycle;
    }
}