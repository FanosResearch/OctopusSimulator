/*
 * File  :      CacheController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../header/CacheController.h"
#include "../header/RequestorsQueues.h"
#include "AddrMapping.h"
namespace ns3
{
    // private controller constructor
    CacheController::CacheController(CacheXml &cacheXml, string &fsm_path, CommunicationInterface *upper_interface,
                                     CommunicationInterface *lower_interface, bool cach2Cache,
                                     vector <int> sharedMemId, CohProtType pType, vector<int>* private_caches_id) :ClockedObj(cacheXml.GetCpuClkNanoSec())
    {
        m_rq_cached = nullptr;
        m_addr_cached = nullptr;
        m_core_order = -1;
        m_cache_cycle = 1;

        m_core_id = cacheXml.GetCacheId();
        m_shared_memory_id = sharedMemId;

        m_dt = cacheXml.GetCtrlClkNanoSec();
        m_clk_skew = m_dt * cacheXml.GetCtrlClkSkew() / 100.00;

	globalQueues_en = cacheXml.GetglobalQueues_en();
	log_enable = cacheXml.GetLogEnable();
    	llc_nbnk = cacheXml.GetLLCBnk();
        m_upper_interface = upper_interface;
        m_lower_interface = lower_interface;

        ReplacementPolicy* policy = Policy::getReplacementPolicy(cacheXml.GetReplcPolicy(), cacheXml.GetNWays());
        // m_cache = new CacheDataHandler(cacheXml);
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
        else if (cacheXml.GetmemArb() == "TDM")
            m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new TDMArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
	    else
            m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new FCFSArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
    }
    
    CacheController::CacheController(CacheXml &cacheXml, string &fsm_path, CommunicationInterface *upper_interface,
                                     CommunicationInterface *lower_interface, bool cach2Cache,
                                     vector <int> sharedMemId, CohProtType pType, int order, vector<int>* private_caches_id): ClockedObj(cacheXml.GetCpuClkNanoSec())
    {
    m_rq_cached = nullptr;
    m_addr_cached = nullptr;
    m_core_order = order;
    m_cache_cycle = 1;

    m_core_id = cacheXml.GetCacheId();
    m_shared_memory_id = sharedMemId;

    m_dt = cacheXml.GetCtrlClkNanoSec();
    m_clk_skew = m_dt * cacheXml.GetCtrlClkSkew() / 100.00;

    globalQueues_en = cacheXml.GetglobalQueues_en();
	log_enable = cacheXml.GetLogEnable();
    llc_nbnk = cacheXml.GetLLCBnk();
    m_upper_interface = upper_interface;
    m_lower_interface = lower_interface;
    
    ReplacementPolicy* policy = Policy::getReplacementPolicy(cacheXml.GetReplcPolicy(), cacheXml.GetNWays());
    // m_cache = new CacheDataHandler(cacheXml);
    m_data_handler = new CacheDataHandler_COTS(cacheXml, policy);

    m_cache_line_size = cacheXml.GetBlockSize();

    m_protocol = Protocols::getNewProtocol(pType, m_data_handler, fsm_path, m_core_id, m_shared_memory_id);

    m_protocol->setglobalQ_en(globalQueues_en);

    m_processing_queue =
    new FRFCFS_Buffer<Message, CoherenceProtocolHandler>(&CoherenceProtocolHandler::getRequestState,
                                        m_protocol,
                                        cacheXml.GetNPendReq());
                                                
    if (cacheXml.GetmemArb() == "RR")                                                                 
    m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new RRArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
    else if (cacheXml.GetmemArb() == "RROF")
    m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new RROFArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
    else if (cacheXml.GetmemArb() == "TDM")
    m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new TDMArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
    else
    m_data_access_arbiter = (private_caches_id == NULL) ? NULL : new FCFSArbiter(private_caches_id, cacheXml.GetDataAccessLatency());
    }

    CacheController::~CacheController()
    {
        delete m_protocol;
        delete m_data_handler;
    }

    void CacheController::cycleProcess()
    {
        // Lazy-init cached singleton pointers (once)
        if (!m_addr_cached)
            m_addr_cached = AddrMapping::getAddrMapping();
        if (globalQueues_en && !m_rq_cached)
            m_rq_cached = RequestorsQueues::getReqQObj()->getRequestorsQueues();

        m_data_handler->updateCycle(m_cache_cycle);
        this->processDataArrayBuffer();
        this->processLogic(); // Call cache controller

        m_cache_cycle++;
        if (m_core_id == 0 && globalQueues_en)
        {
            m_rq_cached->updateClk(m_cache_cycle);
        }
    }

    void CacheController::init()
    {
        m_protocol->initializeCacheStates(); // Initialized Cache Coherence Protocol
    }

    void CacheController::callActionFunction(const ControllerAction &action)
    {
        switch (action.type)
        {
            case ControllerAction::Type::REMOVE_PENDING: this->removePendingAndRespond(action.data); return;
            case ControllerAction::Type::HIT_Action: this->hitAction(action.data); return;
            case ControllerAction::Type::ADD_PENDING: this->addtoPendingRequests(action.data); return;
            case ControllerAction::Type::SEND_BUS_MSG: this->sendBusRequest(action.data); return;
            case ControllerAction::Type::WRITE_BACK: this->performWriteBack(action.data); return;
            case ControllerAction::Type::UPDATE_CACHE_LINE: this->updateCacheLine(action.data); return;
            case ControllerAction::Type::WRITE_CACHE_LINE_DATA: this->writeCacheLineData(action.data); return;
            case ControllerAction::Type::MODIFY_DATA: this->modifyData(action.data); return;
            case ControllerAction::Type::SAVE_REQ_FOR_WRITE_BACK: this->saveReqForWriteBack(action.data); return;
            case ControllerAction::Type::NO_ACTION: this->noAction(action.data); return;
            case ControllerAction::Type::STALL: this->stall(action.data); return;

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

            const vector<ControllerAction> &actions = m_protocol->processRequest(ready_msg);

            for (const ControllerAction &action : actions)
                callActionFunction(action);
        }
    }

    void CacheController::processDataArrayBuffer()
    {
        if(m_data_handler->isReady() && m_data_access_arbiter != NULL)
        {
            Message selected_msg;
            vector<deque<Message>*> messages_pending_data_access; //wrapper vector to use the arbiter
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
                    goto check_replacements;
                }
            }

            if (m_shared_memory_id[0] == 100 && llc_nbnk )
            {
                m_addr_cached->addr_map(&msg);
            }
            msg.source = Message::Source::UPPER_INTERCONNECT;
            if (buf.pushFront(msg))
                m_upper_interface->popFrontMessage();
            if (globalQueues_en)
            {
                if (m_shared_memory_id[0] == 100)
                {
                    m_rq_cached->setRemovalCount(msg.owner, msg.msg_id,2);
                    if(log_enable)
                        cout <<"addRequests2ProcessingQueue: set: " << msg.msg_id << " core: " << msg.owner << " count: "<< m_rq_cached->getRemovalCount (msg.owner, msg.msg_id) <<endl;
                }
            }
        }

	check_replacements:
	this->checkReplacements(buf);

        if (m_lower_interface->peekMessage(&msg))
        {
            //msg.source = Message::Source::LOWER_INTERCONNECT;
            //if (buf.pushBack(msg, FRFCFS_State::NonReady))
            //    m_lower_interface->popFrontMessage();
                if (m_shared_memory_id[0] == 100 && llc_nbnk )
                {
                    m_addr_cached->addr_map(&msg);
                     if ((unsigned int)this->m_core_order == msg.bank_id )
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

    uint64_t CacheController::getAddressKey(uint64_t addr)
    {
        return (addr >> int(log2(this->m_cache_line_size)));
    }

    void CacheController::addtoPendingRequests(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        this->m_pending_cpu_requests[this->getAddressKey(msg->addr)].push(*msg);
        
        if(msg->data != NULL)
            this->m_modifying_data_messages[msg->msg_id] = *msg;

        delete msg;
    }

    void CacheController::removePendingAndRespond(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        auto pending_it = m_pending_cpu_requests.find(getAddressKey(msg->addr));
        if (pending_it != m_pending_cpu_requests.end())
        {
            queue<Message> pending_messages = pending_it->second;
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

                // restore addr before sending out from shared cache
                if (m_shared_memory_id[0] == 100 && llc_nbnk )
                {
                    m_addr_cached->addr_map_restore(&pending_messages.front(), this->m_core_order);
                }
                if (!m_lower_interface->pushMessage(pending_messages.front(), this->m_cache_cycle, MessageType::DATA_RESPONSE))
                {
                    cout << "CacheController: Cannot insert the Msg into lower interface." << endl;
                    exit(0);
                }
                pending_messages.pop();
            }
            this->m_pending_cpu_requests.erase(pending_it);
        }
        else
        { // For the LLC
            if (msg->data == NULL)
            {
                if(!checkReadinessOfCache(*msg, ControllerAction::Type::REMOVE_PENDING, data_ptr))
                    return;
                GenericCacheLine cache_line;
                m_data_handler->readCacheLine(msg->addr, &cache_line);
                msg->copy(cache_line.m_data);
            }

            // restore addr before sending out from shared cache
            if (m_shared_memory_id[0] == 100 && llc_nbnk )
            {
                m_addr_cached->addr_map_restore(msg, this->m_core_order);
            }
            if (!m_lower_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::DATA_RESPONSE))
            {
                cout << "CacheController: Cannot insert the Msg into lower interface." << endl;
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

        // restore addr before sending out from shared cache
        if (m_shared_memory_id[0] == 100 && llc_nbnk )
        {
            m_addr_cached->addr_map_restore(msg, this->m_core_order);
        }

        if (!m_lower_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::DATA_RESPONSE))
        {
            cout << "CacheController: Cannot insert the Msg into lower interface." << endl;
            exit(0);
        }

        delete msg;
    }

    void CacheController::sendBusRequest(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        msg->cycle = this->m_cache_cycle;

        // restore addr before sending out from shared cache
        if (m_shared_memory_id[0] == 100 && llc_nbnk )
        {
            m_addr_cached->addr_map_restore(msg, this->m_core_order);
        }

        if (!m_upper_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::REQUEST))
        {
            cout << "CacheController(id = " << this->m_core_id << "): Cannot insert the Msg into the upper interface FIFO, FIFO is Full" << endl;
            exit(0);
        } else
        {
            if (globalQueues_en)
            {
                if (m_shared_memory_id[0] != 100) //in L1
                {
                    m_rq_cached->addRequest(msg->owner, msg->msg_id,1, msg->addr);
                    unsigned int orig_core;
                    if(log_enable)
                        cout << "sendBusRequest: add: " << msg->msg_id << " core: " << msg->owner << " size: " << m_rq_cached->getRequestorSize(msg->owner) << " type: "<< msg->complementary_value << " 1st: "<< m_rq_cached->getRequest(msg->owner,0, &orig_core) << " clk: "<<this->m_cache_cycle << " data "<<(msg->data == NULL)<<" CL "<< msg->addr<<endl;
                }
            }
        }

        delete msg;
    }

    void CacheController::performWriteBack(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        auto wb_it = this->m_saved_requests_for_wb.find(this->getAddressKey(msg->addr));
        if (wb_it != this->m_saved_requests_for_wb.end())
        {
            msg->owner = wb_it->second.owner;
            msg->msg_id = wb_it->second.msg_id;
            this->m_saved_requests_for_wb.erase(wb_it);
        }

        if(msg->data == NULL)
        {
            if(!checkReadinessOfCache(*msg, ControllerAction::Type::WRITE_BACK, data_ptr))
                return;
            GenericCacheLine cache_line;
            m_data_handler->readCacheLine(msg->addr, &cache_line);
            msg->copy(cache_line.m_data);
        }

        if (msg->owner == this->m_core_id)
            msg->to.push_back(this->m_shared_memory_id[m_addr_cached->get_bnk_bits(msg->addr,this->m_core_id)]); //address map
        else
            msg->to.push_back(msg->owner);

        // restore addr before sending out from shared cache
        if (m_shared_memory_id[0] == 100 && llc_nbnk )
        {
            m_addr_cached->addr_map_restore(msg, this->m_core_order);
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

        bool has_data = (msg->data != NULL);// && (msg->source == Message::Source::UPPER_INTERCONNECT);
        if (!has_data || !cache_line->valid) // || msg->data_size < m_cache_line_size)
        {
            if (!m_data_handler->updateLineBits(msg->addr, cache_line))
                ((CacheDataHandler_COTS*)m_data_handler)->writeLine2MSHR(msg->addr, cache_line);//ToDo: Should be move to CacheDataHandler_COTS
            if(msg->data != NULL)
            {
                if (globalQueues_en && m_shared_memory_id[0] != 100)
                {
                    m_rq_cached->removeRequest(msg->owner, msg->msg_id);
                    if(log_enable)
                        cout << "updateCacheLine: remove: " << msg->msg_id << " core: " << msg->owner << " size: " << m_rq_cached->getRequestorSize(msg->owner) << " id: "<< this->m_core_id << " to: "<< msg->to.size()<< endl;

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
        }else
        {
            if (globalQueues_en)
            {
                m_rq_cached->removeRequest(msg->owner, msg->msg_id);
                if(log_enable)
                    cout << "writeCacheLineData: remove: " << msg->msg_id << " core: " << msg->owner << " size: " << m_rq_cached->getRequestorSize(msg->owner) << " id: "<< this->m_core_id << " to: "<< msg->to.size()<< endl;

            }
        }

        cache_line->~GenericCacheLine();    //explicit call for the destructor due to the use of placement new
        msg->~Message();                    //explicit call for the destructor due to the use of placement new

        delete[] (uint8_t *)data_ptr;
    }

    void CacheController::modifyData(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        Message data_msg;

        auto mod_it = m_modifying_data_messages.find(msg->msg_id);
        if(mod_it != m_modifying_data_messages.end())
            data_msg = mod_it->second;
        else if ((msg->source == Message::Source::LOWER_INTERCONNECT) && (msg->data != NULL))
            data_msg = *msg;
        else
        {
            delete[] (uint8_t *)data_ptr;
            return;
        }

        m_data_handler->modifyData(data_msg.addr, data_msg.data, data_msg.data_size);
        m_modifying_data_messages.erase(msg->msg_id);

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
                        2,//   (uint64_t)CpuFIFO::REQTYPE::REPLACE, // Complementary_value //TODO: add enum for Request type
                          (uint16_t)this->m_core_id);          // Owner
            msg.to.push_back((uint16_t)this->m_shared_memory_id[m_addr_cached->get_bnk_bits(evicted_address,this->m_core_id)]);
            if (buf.pushBack(msg, FRFCFS_State::NonReady))
                ((CacheDataHandler_COTS*)m_data_handler)->addressOfLinePendingWB(true, &evicted_address);

            for(int i = 0; i < (int)m_data_access_buffer.size(); )
            {
                if(getAddressKey(m_data_access_buffer[i].addr) == getAddressKey(evicted_address))
                {
                    callActionFunction(m_data_access_action[m_data_access_buffer[i].msg_id]);
                    m_data_access_action.erase(m_data_access_buffer[i].msg_id);  //after erasing the looping counter shouldn't get incremented
                    m_data_access_buffer.erase(m_data_access_buffer.begin() + i);
                }
                else
                    i++;
            }
        }
    }

    void CacheController::initialize(uint64_t address, const uint8_t* data, int size) //for Initializable
    {
        GenericCacheLine cache_line;
        if (!this->m_data_handler->readLineBits(address, &cache_line))
        {
            if (m_data_handler->findEmptyWay(address) == -1)
            {
                cout << "ERROR: Cache doesn't fit the data ... increase the cache size" << endl;
                exit(0);
            }

            this->m_protocol->createDefaultCacheLine(address, &cache_line);
            cache_line.modifyData(data, address & (cache_line.m_block_size - 1), size);
            this->m_data_handler->writeCacheLine_bypassLatency(address, &cache_line, true);
        }
        else    
        {
            // cout << "Found!" << endl;
            // if(cache_line.owner_id == this->m_core_id || cache_line.owner_id == -1)
            //     this->m_data_handler->modifyData(address, data, size, true);
            // else
            //     m_children_controllers[cache_line.owner_id]->initialize_2(address, data, size);
            
            this->m_data_handler->modifyData(address, data, size, true);
            for(int i = 0; i < m_children_controllers.size(); i++)
                m_children_controllers[i]->initialize_2(address, data, size);
        }
    }

    void CacheController::initialize_2(uint64_t address, const uint8_t* data, int size) //for Initializable
    {
        GenericCacheLine cache_line;
        if (this->m_data_handler->readLineBits(address, &cache_line))
        {
            this->m_data_handler->modifyData(address, data, size, true);
        }
    }

    void CacheController::read(uint64_t address, uint8_t* data)
    {
        GenericCacheLine cache_line;
        if (this->m_data_handler->readCacheLine(address, &cache_line, true))
        {
            if(cache_line.owner_id == this->m_core_id || cache_line.owner_id == -1)
                memcpy(data, cache_line.m_data, cache_line.m_block_size);
            else
                m_children_controllers[cache_line.owner_id]->read(address, data);    
        }
        // else    
        // {
        //     cout << "Error requested line is not found!" << endl;
        //     exit(0);
        // }
    }

}
