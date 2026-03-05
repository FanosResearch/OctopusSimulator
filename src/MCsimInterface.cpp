/*
 * File  :      MCsimInterface.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On August 4, 2022
 */

#include "../header/MCsimInterface.h"
#include "../header/RequestorsQueues.h"

namespace ns3
{
    MCsimInterface::MCsimInterface(MCoreSimProjectXml &projectXml, CommunicationInterface *lower_interface, vector<int> llc_id):ClockedObj(projectXml.GetDRAMCtrlClkNanoSec())
    {
        m_id = projectXml.GetDRAMId()[0];
        m_llc_id = llc_id;

        m_dt = projectXml.GetDRAMCtrlClkNanoSec();
        m_clk_skew = projectXml.GetDRAMCtrlClkSkew();
        m_clk_cycle = 1;
        m_log_enable = projectXml.GetLogFileGenEnable();
        m_llc_line_size = projectXml.GetSharedCaches().begin()->GetBlockSize();

        m_read_count = 0;
        m_write_count = 0;

        m_lower_interface = lower_interface;

        m_processing_queue = new FRFCFS_Buffer<Message, MCsimInterface>(&MCsimInterface::getRequestState, this);

        /******************************************** Initialization of MCsim ********************************************/
        unsigned int num_cores = projectXml.GetNumPrivCore();
        m_loggerPath = projectXml.GetLoggerPath();
        string mem_system = projectXml.GetmemSystem();
        string sys_path = projectXml.GetsysPath();
        string sys_init_file = sys_path+"/MCsim/system/"+mem_system+"/"+mem_system+".ini";
        string sys_gen = projectXml.GetmcsimGen();
        string sys_speed = projectXml.GetmcsimSpeed();
        string sys_size = projectXml.Getm_mcsimSize();
        unsigned int sys_channel = projectXml.GetmcsimChannel();
        unsigned int sys_rank = projectXml.GetmcsimRank();
        float sys_freq = projectXml.GetmcsimCpuFreq();

        m_mcsim = MCsim::getMemorySystemInstance(
            num_cores,
            sys_init_file, // this should be parameterized
            sys_gen,
            sys_speed,
            sys_size,
            sys_channel,
            sys_rank); // 2048*4 = 4 ranks
        //m_mcsim->setCPUClockSpeed(0);
        m_mcsim->setCPUClockSpeed((uint64_t) sys_freq*1e9); //freq in GHz

        m_requestors_queues = m_mcsim->getRequestorsQueues();
        RequestorsQueues::getReqQObj()->setRequestorsQueues(m_requestors_queues);
        RequestorsQueues::getReqQObj()->getRequestorsQueues()->setLoggerPath(m_loggerPath);
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
            //if (m_mcsim->addRequest(ready_msg.owner, ready_msg.addr, ready_msg.data == NULL, m_llc_line_size)) // 0 -> Read, 1 -> Write
            if (m_mcsim->addRequest(ready_msg.owner, ready_msg.addr, ready_msg.data == NULL, m_llc_line_size, ready_msg.msg_id)) // 0 -> Read, 1 -> Write
            {
                // if(m_log_enable)
                //     cout << "MCsimInterface msg_in "<<ready_msg.msg_id << " core "<< ready_msg.owner<< " clk "<<m_clk_cycle << endl; 
                if(ready_msg.data == NULL)
                    m_pending_requests.push_back(ready_msg);
                else
                {
                    // Track pending writes so write_callback can remove from RequestorsQueues.
                    // For RROF, CommandScheduler_RROF also removes after WR CAS (harmless double-remove guarded below).
                    // For FRFCFS, this is the only removal path - without it, dirty writebacks
                    // leak in RequestorsQueues, causing unbounded queue growth.
                    m_pending_writes.push_back(ready_msg);
                }
            }
            else
            {
                cout << "MCsimInterface: Error failed to add request to MCsim" << endl;
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
            m_output_buffer.pop_front();
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
                //msg.to.push_back((uint16_t)m_llc_id[0]);               // To
                msg.llc_addr_restore = m_pending_requests[i].llc_addr_restore;
                if (msg.owner >= 50)
                    msg.to.push_back((uint16_t)m_llc_id[msg.owner-50]); 
                else
                    msg.to.push_back((uint16_t)m_llc_id[AddrMapping::getAddrMapping()->get_bnk_bits(m_pending_requests[i].addr,m_pending_requests[i].owner)]); 
                msg.copy((uint8_t *)&data);
                m_output_buffer.push_back(msg);

                // if(m_log_enable)
                //     cout <<"MCsimInterface msg_out " << msg.msg_id << " core " << msg.owner << " clk "<<m_clk_cycle << endl;

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

    void MCsimInterface::write_callback(unsigned id, uint64_t address, uint64_t clock_cycle)
    {
        m_write_count++;

        for (int i = 0; i < (int)m_pending_writes.size(); i++)
        {
            if (m_pending_writes[i].addr == address)
            {
                // Guard: RROF's CommandScheduler may have already removed this write
                if (m_requestors_queues->isRequestExist(m_pending_writes[i].owner, m_pending_writes[i].msg_id, NULL, NULL) >= 0)
                    m_requestors_queues->removeRequest(m_pending_writes[i].owner, m_pending_writes[i].msg_id);
                m_pending_writes.erase(m_pending_writes.begin() + i);
                return;
            }
        }
    }
}
