/*
 * File  :      CacheController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../header/CacheController.h"
#include "../header/RequestorsQueues.h"
namespace ns3
{
    // private controller constructor
    //CacheController::CacheController(CacheXml &cacheXml, string &fsm_path, CommunicationInterface *upper_interface,
    //                                 CommunicationInterface *lower_interface, bool cach2Cache,
    //                                int sharedMemId, CohProtType pType, vector<int>* private_caches_id) : ClockedObj(cacheXml.GetCtrlClkNanoSec())
    CacheController::CacheController(CacheXml &cacheXml, string &fsm_path, CommunicationInterface *upper_interface,
                                     CommunicationInterface *lower_interface, bool cach2Cache,
                                     vector <int> sharedMemId, CohProtType pType, vector<int>* private_caches_id) :ClockedObj(1)
    {
        m_core_order = -1;
        m_cache_cycle = 1;

        m_core_id = cacheXml.GetCacheId();
        m_shared_memory_id = sharedMemId;

        m_dt = cacheXml.GetCtrlClkNanoSec();
        m_clk_skew = m_dt * cacheXml.GetCtrlClkSkew() / 100.00;

        end_to_end = cacheXml.GetEndToEnd();
        log_enable = cacheXml.GetLogEnable();

        m_upper_interface = upper_interface;
        m_lower_interface = lower_interface;

         ReplacementPolicy* policy = Policy::getReplacementPolicy(cacheXml.GetReplcPolicy(), cacheXml.GetNWays());

        // m_cache = new CacheDataHandler(cacheXml);
        m_data_handler = new CacheDataHandler_COTS(cacheXml, policy);

        m_cache_line_size = cacheXml.GetBlockSize();

        m_protocol = Protocols::getNewProtocol(pType, m_data_handler, fsm_path, m_core_id, m_shared_memory_id);
        m_protocol_type = pType;

        m_processing_queue =
            new FRFCFS_Buffer<Message, CoherenceProtocolHandler>(&CoherenceProtocolHandler::getRequestState,
                                                                 m_protocol,
                                                                 cacheXml.GetNPendReq());
        if (cacheXml.GetmemArb() == "RR")                                                                 
            m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new RRArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
        else if (cacheXml.GetmemArb() == "RROF")
            m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new RROFArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
        else if (cacheXml.GetmemArb() == "FCFS" || cacheXml.GetmemArb() == "FRFCFS")
            m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new FCFSArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
        else if (cacheXml.GetmemArb() == "TDM")
            m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new TDMArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
    }
    CacheController::CacheController(CacheXml &cacheXml, string &fsm_path, CommunicationInterface *upper_interface,
                                     CommunicationInterface *lower_interface, bool cach2Cache,
                                     vector <int> sharedMemId, CohProtType pType, int order, vector<int>* private_caches_id): ClockedObj(1)
    {
        m_core_order = order;
        m_cache_cycle = 1;

        m_core_id = cacheXml.GetCacheId();
        m_shared_memory_id = sharedMemId;

        m_dt = cacheXml.GetCtrlClkNanoSec();
        m_clk_skew = m_dt * cacheXml.GetCtrlClkSkew() / 100.00;

        end_to_end = cacheXml.GetEndToEnd();
        log_enable = cacheXml.GetLogEnable();

        m_upper_interface = upper_interface;
        m_lower_interface = lower_interface;

         ReplacementPolicy* policy = Policy::getReplacementPolicy(cacheXml.GetReplcPolicy(), cacheXml.GetNWays());

        // m_cache = new CacheDataHandler(cacheXml);
        //m_data_handler = new CacheDataHandler_COTS(cacheXml);
        m_data_handler = new CacheDataHandler_COTS(cacheXml, policy);

        m_cache_line_size = cacheXml.GetBlockSize();

        m_protocol = Protocols::getNewProtocol(pType, m_data_handler, fsm_path, m_core_id, m_shared_memory_id);

        m_processing_queue =
            new FRFCFS_Buffer<Message, CoherenceProtocolHandler>(&CoherenceProtocolHandler::getRequestState,
                                                                 m_protocol,
                                                                 cacheXml.GetNPendReq());
                                                                         
        if (cacheXml.GetmemArb() == "RR")                                                                 
            m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new RRArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
        else if (cacheXml.GetmemArb() == "RROF")
            m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new RROFArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
        else if (cacheXml.GetmemArb() == "FCFS" || cacheXml.GetmemArb() == "FRFCFS")
            m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new FCFSArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
        else if (cacheXml.GetmemArb() == "TDM")
            m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new TDMArbiter(private_caches_id, cacheXml.GetDataAccessLatency());

    }

    CacheController::~CacheController()
    {
        delete m_protocol;
        delete m_data_handler;
    }

    void CacheController::cycleProcess()
    {
        m_data_handler->updateCycle(m_cache_cycle);
        this->processDataArrayBuffer();
        this->processLogic(); // Call cache controller

        m_cache_cycle++;
        if (m_core_id == 0)
        {
            RequestorsQueues::getReqQObj()->getRequestorsQueues()->updateClk(m_cache_cycle);
        }
    }

    void CacheController::init()
    {
        m_protocol->initializeCacheStates(); // Initialized Cache Coherence Protocol
    }

    void CacheController::callActionFunction(ControllerAction action)
    {
        switch (action.type)
        {
            case ControllerAction::Type::REMOVE_PENDING: this->removePendingAndRespond(action.data); return; //0
            case ControllerAction::Type::HIT_Action: this->hitAction(action.data); return; //1
            case ControllerAction::Type::ADD_PENDING: this->addtoPendingRequests(action.data); return; //2
            case ControllerAction::Type::SEND_BUS_MSG: this->sendBusRequest(action.data); return; //3
            case ControllerAction::Type::WRITE_BACK: this->performWriteBack(action.data); return; //4
            case ControllerAction::Type::UPDATE_CACHE_LINE: this->updateCacheLine(action.data); return; //5
            case ControllerAction::Type::WRITE_CACHE_LINE_DATA: this->writeCacheLineData(action.data); return; //6
            case ControllerAction::Type::SAVE_REQ_FOR_WRITE_BACK: this->saveReqForWriteBack(action.data); return; //7
            case ControllerAction::Type::NO_ACTION: this->noAction(action.data); return; //8
            case ControllerAction::Type::STALL: this->stall(action.data); return; //14

            default: cout << "CacheController: Invalid Action Type!!" << endl; return;
        }
    }

    void CacheController::processLogic()
    {
        this->addRequests2ProcessingQueue(*m_processing_queue);

        while(true)
        {
            Message ready_msg;
            if (m_processing_queue->getFirstReady(&ready_msg) == false)
                return;

            if(ready_msg.source == Message::Source::LOWER_INTERCONNECT)
                Logger::getLogger()->updateRequest(ready_msg.msg_id, Logger::EntryId::CACHE_CHECKPOINT);

            vector<ControllerAction> actions = m_protocol->processRequest(ready_msg);

            for (ControllerAction action : actions)
                callActionFunction(action);
        }
    }

    void CacheController::processDataArrayBuffer()
    {
        if(m_data_handler->isReady() && m_data_access_arbiter != NULL)
        {
            Message selected_msg;
            vector<vector<Message>*> messages_pending_data_access; //wrapper vector to use the arbiter
            messages_pending_data_access.push_back(&m_data_access_buffer);

            bool msg_available = m_data_access_arbiter->elect(m_cache_cycle, 
                                                              messages_pending_data_access, &selected_msg);
            if(msg_available)
            {
                Logger::getLogger()->updateRequest(selected_msg.msg_id, Logger::EntryId::CACHE_CHECKPOINT);
                callActionFunction(m_data_access_action[selected_msg.msg_id]);
                m_data_access_action.erase(selected_msg.msg_id);
            }
        }
    }

    void CacheController::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf)
    {
        Message msg;

        if (m_upper_interface->peekMessage(&msg))
        {
            // Fix: Filter out data messages not destined for this core.
            // This prevents L1s from processing LLC WriteBacks (destined for Memory) as OwnData.
            if (msg.data != NULL)
            {
                bool is_dest = false;
                for (uint16_t dest : msg.to)
                {
                    if (dest == m_core_id)
                    {
                        is_dest = true;
                        break;
                    }
                }
                if (!is_dest)
                {
                    m_upper_interface->popFrontMessage();
                    // Skip processing this message and move to replacements/lower interface
                    goto check_replacements;
                }
            }

            msg.source = Message::Source::UPPER_INTERCONNECT;
            if (buf.pushFront(msg))
                m_upper_interface->popFrontMessage();

            if (end_to_end)
            {
                if (m_shared_memory_id[0] == 100)
                {
                    RequestorsQueues::getReqQObj()->getRequestorsQueues()->setRemovalCount(msg.owner, msg.msg_id,2);
                    if(log_enable)
                        cout <<"addRequests2ProcessingQueue: set: " << msg.msg_id << " core: " << msg.owner << " count: "<< RequestorsQueues::getReqQObj()->getRequestorsQueues()->getRemovalCount (msg.owner, msg.msg_id) <<endl;
                }
            }
        }

        check_replacements:
        this->checkReplacements(buf);
        
        if (m_lower_interface->peekMessage(&msg))
        {
            // msg.source = Message::Source::LOWER_INTERCONNECT;
            // if (buf.pushBack(msg, FRFCFS_State::NonReady))
            //     m_lower_interface->popFrontMessage();

            if (end_to_end)
            {
                if (m_shared_memory_id[0] == 100)
                {
                    
                    if ((unsigned int)this->m_core_order == addrMapping( msg.addr))
                    {
                        msg.source = Message::Source::LOWER_INTERCONNECT;
                        if (buf.pushBack(msg, FRFCFS_State::NonReady))
                            m_lower_interface->popFrontMessage();

                    }
                    else
                    {
                        m_lower_interface->popFrontMessage();
                    }
                }
                else
                {
                    msg.source = Message::Source::LOWER_INTERCONNECT;
                    if (buf.pushBack(msg, FRFCFS_State::NonReady))
                        m_lower_interface->popFrontMessage();
                }
            }
        }
    }

    uint64_t CacheController::getAddressKey(uint64_t addr)
    {
        return (addr >> int(log2(this->m_cache_line_size)));
    }

    void CacheController::addtoPendingRequests(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        this->m_pending_cpu_requests[this->getAddressKey(msg->addr)].push(*msg);

        delete msg;
    }

    void CacheController::removePendingAndRespond(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        if (m_pending_cpu_requests.find(getAddressKey(msg->addr)) != m_pending_cpu_requests.end())
        {
            queue<Message> pending_messages = this->m_pending_cpu_requests[this->getAddressKey(msg->addr)];
            if (pending_messages.size() > 1)
                cout << "How !!!!!1" << endl;
            while (!pending_messages.empty())
            {
                if (msg->data != NULL)
                {
                    pending_messages.front().complementary_value = msg->complementary_value;
                    pending_messages.front().to = msg->to;
                    pending_messages.front().copy(msg->data);
                }
                else // This can happen while moving from O to M
                    cout << "CacheController: Remove from pending without data" << endl;

                if (!m_lower_interface->pushMessage(pending_messages.front(), this->m_cache_cycle, MessageType::DATA_RESPONSE))
                {
                    cout << "CacheController,removePendingAndRespond1: Cannot insert the Msg into lower interface." << endl;
                    exit(0);
                }
                pending_messages.pop();
            }
            this->m_pending_cpu_requests.erase(this->getAddressKey(msg->addr));
        }
        else
        { // For the LLC
            if (msg->data == NULL)
            {
                // check CL in m_data_access_buffer for forwarding
                for (unsigned int i=0 ; i<m_data_access_buffer.size();i++)
                {
                    if (m_data_access_buffer[i].addr == msg->addr && m_data_access_buffer[i].complementary_value == 2)
                    {
                        msg->copy(m_data_access_buffer[i].data);
                        if (!m_lower_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::DATA_RESPONSE))
                        {
                            cout << "CacheController,removePendingAndRespond2: Cannot insert the Msg into lower interface." << endl;
                            exit(0);
                        }
                        return;
                    }
                }

                // otherwise access the cache bank to read data
                if(!checkReadinessOfCache(*msg, ControllerAction::Type::REMOVE_PENDING, data_ptr))
                    return;
                GenericCacheLine cache_line;
                m_data_handler->readCacheLine(msg->addr, &cache_line);
                msg->copy(cache_line.m_data);
            }
            if (!m_lower_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::DATA_RESPONSE))
            {
                cout << "CacheController,removePendingAndRespond3: Cannot insert the Msg into lower interface." << endl;
                exit(0);
            }

        }

        delete msg;
    }

    void CacheController::hitAction(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        if (msg->data == NULL)
        {
            if(!checkReadinessOfCache(*msg, ControllerAction::Type::HIT_Action, data_ptr))
                return;
            GenericCacheLine cache_line;
            m_data_handler->readCacheLine(msg->addr, &cache_line);
            msg->copy(cache_line.m_data);
        }

        if (!m_lower_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::DATA_RESPONSE))
        {
            cout << "CacheController,hitAction: Cannot insert the Msg into lower interface." << endl;
            exit(0);
        }

        delete msg;
    }

    void CacheController::sendBusRequest(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        msg->cycle = this->m_cache_cycle;

        if (!m_upper_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::REQUEST))
        {
            cout << "CacheController(id = " << this->m_core_id << "): Cannot insert the Msg into the upper interface FIFO, FIFO is Full" << endl;
            exit(0);
        }
        else
        {
            if (end_to_end)
            {
                if (m_shared_memory_id[0] != 100) //in L1
                {
                    RequestorsQueues::getReqQObj()->getRequestorsQueues()->addRequest(msg->owner, msg->msg_id,1, msg->addr);
                    //RequestorsQueues::getReqQObj()->getRequestorsQueues()->addRequest(msg->owner, msg->msg_id,1);
                    unsigned int orig_core;
                    if(log_enable)
                        cout << "sendBusRequest: add: " << msg->msg_id << " core: " << msg->owner << " size: " << RequestorsQueues::getReqQObj()->getRequestorsQueues()->getRequestorSize(msg->owner) << " type: "<< msg->complementary_value << " 1st: "<< RequestorsQueues::getReqQObj()->getRequestorsQueues()->getRequest(msg->owner,0, &orig_core) << " clk: "<<this->m_cache_cycle << " data "<<(msg->data == NULL)<<" CL "<< msg->addr<<endl;
                }
            }
        }

        delete msg;
    }

    void CacheController::performWriteBack(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        if (this->m_saved_requests_for_wb.find(this->getAddressKey(msg->addr)) !=
            this->m_saved_requests_for_wb.end())
        {
            msg->owner = this->m_saved_requests_for_wb[this->getAddressKey(msg->addr)].owner;
            msg->msg_id = this->m_saved_requests_for_wb[this->getAddressKey(msg->addr)].msg_id;
            this->m_saved_requests_for_wb.erase(this->getAddressKey(msg->addr));
        }

        if(msg->data == NULL)
        {
            if(!checkReadinessOfCache(*msg, ControllerAction::Type::WRITE_BACK, data_ptr))
                return;
            GenericCacheLine cache_line;
            m_data_handler->readCacheLine(msg->addr, &cache_line);
            msg->copy(cache_line.m_data);
        }

        if (msg->owner == this->m_core_id) // write back for replacement
        {
            //msg->to.push_back(this->m_shared_memory_id[0]);
            msg->to.push_back(this->m_shared_memory_id[addrMapping(msg->addr)]);
        }
        else
        {
            msg->to.push_back(msg->owner);
            //if(msg->complementary_value == 2 && this->m_protocol_type == CohProtType::SNOOP_MESI) // SA: fix for MESI
            //    msg->complementary_value = 5;
        }

        if (!m_upper_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::DATA_RESPONSE))
        {
            cout << "CacheController: Cannot insert the Msg into BusTxResp FIFO, FIFO is Full" << endl;
            exit(0);
        }
        delete msg;
    }

    void CacheController::updateCacheLine(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        GenericCacheLine *cache_line = (GenericCacheLine *)((uint8_t *)data_ptr + sizeof(Message));

        if (msg->data == NULL || !cache_line->valid)
        {
            if (!m_data_handler->updateLineBits(msg->addr, cache_line))
                ((CacheDataHandler_COTS*)m_data_handler)->writeLine2MSHR(msg->addr, cache_line);

            if(msg->data != NULL)
            {
                if (end_to_end && m_shared_memory_id[0] != 100)
                {
                    RequestorsQueues::getReqQObj()->getRequestorsQueues()->removeRequest(msg->owner, msg->msg_id);
                    if(log_enable)
                        cout << "updateCacheLine: remove: " << msg->msg_id << " core: " << msg->owner << " size: " << RequestorsQueues::getReqQObj()->getRequestorsQueues()->getRequestorSize(msg->owner) << " id: "<< this->m_core_id << " to: "<< msg->to.size()<< endl;
                }
            }
        }       
        else
        {
            m_data_handler->updateLineBits(msg->addr, cache_line);
            writeCacheLineData(data_ptr);
            return;
        }

        cache_line->~GenericCacheLine();    //explicit call for the destructor due to the use of placement new
        msg->~Message();                    //explicit call for the destructor due to the use of placement new

        delete[] (uint8_t *)data_ptr;
    }
    
    void CacheController::writeCacheLineData(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        GenericCacheLine *cache_line = (GenericCacheLine *)((uint8_t *)data_ptr + sizeof(Message));

        if(!checkReadinessOfCache(*msg, ControllerAction::Type::WRITE_CACHE_LINE_DATA, data_ptr))
            return;


        if (!m_data_handler->updateLineData(msg->addr, msg->data))
        {
            cout << "CacheController: update data of an unfound line" << endl;
            exit(0);
        }
        else
        {
            // NOTE: Both L1 and LLC call removeRequest here intentionally.
            // The removal count is pre-set to 2 (via setRemovalCount) for flows where
            // both L1 and LLC go through this path (e.g., DRAM responses, EorM+GetS writebacks).
            // For flows where only L1 reaches this path, the count stays at 1.
            if (end_to_end)
            {
                    RequestorsQueues::getReqQObj()->getRequestorsQueues()->removeRequest(msg->owner, msg->msg_id);
                    if(log_enable)
                        cout << "writeCacheLineData: remove: " << msg->msg_id << " core: " << msg->owner << " size: " << RequestorsQueues::getReqQObj()->getRequestorsQueues()->getRequestorSize(msg->owner) << " id: "<< this->m_core_id << " to: "<< msg->to.size()<< endl;
            }
        }

        cache_line->~GenericCacheLine();    //explicit call for the destructor due to the use of placement new
        msg->~Message();                    //explicit call for the destructor due to the use of placement new

        delete[] (uint8_t *)data_ptr;
    }
    
    void CacheController::saveReqForWriteBack(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        this->m_saved_requests_for_wb[this->getAddressKey(msg->addr)] = *msg;

        delete msg;
    }

    void CacheController::stall(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        
        if (!m_processing_queue->pushBack(*msg, FRFCFS_State::NonReady))
        {
            cout << "CacheController: error there is no free space to push request to processing queue" << endl;
            exit(0);
        }
        delete msg;
    }

    void CacheController::initializeCacheData(std::vector<std::string> &tracePaths)
    {
        uint64_t mockup_data = 1;
        for (string path : tracePaths)
        {
            ifstream file(path);
            string line;

            if (!file.is_open())
            {
                cout << "ERROR: Can't open trace file" << endl;
                exit(0);
            }

            while (getline(file, line))
            {
                unsigned long long address;

                sscanf(line.c_str(), "%llx", &address);

                if (!this->m_data_handler->readLineBits(address))
                {

                    if (m_data_handler->findEmptyWay(address) == -1)
                    {
                        cout << "ERROR: Cache is not prefect ... increase the cache size" << endl;
                        exit(0);
                    }

                    GenericCacheLine cache_line;
                    this->m_protocol->createDefaultCacheLine(address, &cache_line);
                    memcpy(cache_line.m_data, &mockup_data, sizeof(mockup_data));

                    this->m_data_handler->writeCacheLine_bypassLatency(address, &cache_line);
                    mockup_data++;
                }
            }
            file.close();
        }
    }

    bool CacheController::checkReadinessOfCache(Message &msg, ControllerAction::Type type, void *data_ptr)
    {
        if(!m_data_handler->isReady(msg.addr))
        {
            // check if the pushed msg is WB and there is an outstanding WB to the same CL-> discard the older
            if(msg.complementary_value == 2)
            {
                for (unsigned int i=0; i <m_data_access_buffer.size(); i++)
                {
                    if (m_data_access_buffer[i].addr==msg.addr && m_data_access_buffer[i].complementary_value == 2)
                    {
                        m_data_access_action.erase(m_data_access_buffer[i].msg_id);
                        m_data_access_buffer.erase(m_data_access_buffer.begin() + i);
                        break;
                    }
                }
            }

            m_data_access_buffer.push_back(msg);
            m_data_access_action[msg.msg_id] = ControllerAction{.type = type,
                                                               .data = data_ptr};
            return false;
        }
        return true;
    }

    void CacheController::checkReplacements(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf)
    {
        uint64_t evicted_address = 0;
        Message msg;

        if (((CacheDataHandler_COTS*)m_data_handler)->addressOfLinePendingWB(false, &evicted_address))
        {
            msg = Message(IdGenerator::nextReqId(),            // Id
                          evicted_address,                     // Addr
                          m_cache_cycle,                       // Cycle
                          2,//(uint64_t)CpuFIFO::REQTYPE::REPLACE, // Complementary_value
                          (uint16_t)this->m_core_id);          // Owner
            msg.to.push_back((uint16_t)this->m_shared_memory_id[addrMapping(evicted_address)]);
            if (buf.pushBack(msg, FRFCFS_State::NonReady))
                ((CacheDataHandler_COTS*)m_data_handler)->addressOfLinePendingWB(true, &evicted_address);

            for(int i = 0; i < (int)m_data_access_buffer.size(); )
            {
                if(getAddressKey(m_data_access_buffer[i].addr) == getAddressKey(evicted_address))
                {
                    callActionFunction(m_data_access_action[m_data_access_buffer[i].msg_id]);
                    m_data_access_action.erase(m_data_access_buffer[i].msg_id);
                    m_data_access_buffer.erase(m_data_access_buffer.begin() + i);
                }
                else
                {
                    i++;
                }
            }
        }
    }

    unsigned int CacheController::addrMapping (uint64_t addr)
    {
        unsigned int pos = 17; //bit number+1
        unsigned int numBits = 3;
        return (((1 << numBits) - 1) & (addr >> (pos - 1)));
    }

    void CacheController::setCoreOrder (int order)
    {
        m_core_id = order;
    }

}
