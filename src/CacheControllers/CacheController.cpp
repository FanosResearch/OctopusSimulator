/*
 * File  :      CacheController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../../header/CacheControllers/CacheController.h"

namespace octopus
{
    // private controller constructor
    CacheController::CacheController(ParametersMap map, CommunicationInterface *upper_interface, 
                                     CommunicationInterface *lower_interface, string pname, string config_path, string name) 
    : BaseController(map, upper_interface, lower_interface, pname, config_path, name)
    {   
        //Parameters initialization
        vector<int>* arbiter_candidates_ids = NULL; //ids of the cores subjected to data access arbiteration
        int processing_queue_size = std::get<int>(parameters.at(STRINGIFY(processing_queue_size)).value);
        string arbiter_type = std::get<string>(parameters.at(STRINGIFY(arbiter_type)).value);
        string protocol_type = std::get<string>(parameters.at(STRINGIFY(protocol_type)).value);
        string fsm_filename = std::get<string>(parameters.at(STRINGIFY(fsm_filename)).value);
        string fsm_path = string(FSM_PATH) + fsm_filename + ".csv";

        if(arbiter_type != STRINGIFY(NULL))
            arbiter_candidates_ids = new vector<int>(std::get<vector<int>>(parameters.at(STRINGIFY(arbiter_candidates_ids)).value));
        
        //Constructor
        delete m_data_handler;
        delete m_protocol;
        delete m_processing_queue;

        m_data_handler = new CacheDataHandler_COTS(getSubMap(STRINGIFY(m_data_handler)), parent_name + "." + name);
        m_protocol = Protocols::getNewProtocol(protocol_type, m_data_handler, fsm_path, m_id, m_shared_memory_id);

        m_processing_queue =
            new FRFCFS_Buffer<Message, CoherenceProtocolHandler>(&CoherenceProtocolHandler::getRequestState,
                                                                 m_protocol,
                                                                 processing_queue_size);

        if(arbiter_type == STRINGIFY(NULL))
            m_data_access_arbiter = NULL;
        else if(arbiter_type == STRINGIFY(RRArbiter))                                                    
            m_data_access_arbiter = new RRArbiter(arbiter_candidates_ids, m_data_handler->getDataAccessLatency());
        else if(arbiter_type == STRINGIFY(FCFSArbiter))                                                    
            m_data_access_arbiter = new FCFSArbiter(arbiter_candidates_ids, m_data_handler->getDataAccessLatency());

        action_functions[ControllerAction::Type::WRITE_CACHE_LINE_DATA] = [&](void* ptr) {writeCacheLineData(ptr);};
        action_functions[ControllerAction::Type::MODIFY_DATA] = [&](void* ptr) {modifyData(ptr);};
        action_functions[ControllerAction::Type::SAVE_REQ_FOR_WRITE_BACK] = [&](void* ptr) {saveReqForWriteBack(ptr);};
        action_functions[ControllerAction::Type::NO_ACTION] = [&](void* ptr) {noAction(ptr);};
        action_functions[ControllerAction::Type::STALL] = [&](void* ptr) {stall(ptr);};
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
                
                auto action = m_data_access_action[selected_msg.msg_id];
                action_functions[action.type](action.data);
                m_data_access_action.erase(selected_msg.msg_id);
            }
        }
    }

    void CacheController::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf)
    {
        BaseController::addRequests2ProcessingQueue(buf);
        this->checkReplacements(buf);
    }

    void CacheController::addtoPendingRequests(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        
        if(msg->data != NULL)
            this->m_modifying_data_messages[msg->msg_id] = *msg;

        BaseController::addtoPendingRequests(data_ptr);
    }

    void CacheController::hitAction(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        if (msg->data == NULL && 
            !checkReadinessOfCache(*msg, ControllerAction::Type::HIT_Action, data_ptr))
                return;
        
        BaseController::hitAction(data_ptr);
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

        if(msg->data == NULL &&
           !checkReadinessOfCache(*msg, ControllerAction::Type::WRITE_BACK, data_ptr))
                return;

        BaseController::performWriteBack(data_ptr);
    }

    void CacheController::updateCacheLine(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        GenericCacheLine *cache_line = (GenericCacheLine *)((uint8_t *)data_ptr + sizeof(Message));

        bool has_data = (msg->data != NULL);
        if (!has_data || !cache_line->valid || msg->data_size < m_data_handler->getBlockSize())
        {
            if (!m_data_handler->updateLineBits(msg->addr, cache_line))
                ((CacheDataHandler_COTS*)m_data_handler)->writeLine2MSHR(msg->addr, cache_line);//ToDo: Should be move to CacheDataHandler_COTS
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

        cache_line->~GenericCacheLine();    //explicit call for the destructor due to the use of placement new
        msg->~Message();                    //explicit call for the destructor due to the use of placement new

        delete[] (uint8_t *)data_ptr;
    }

    void CacheController::modifyData(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        Message data_msg;

        if(m_modifying_data_messages.find(msg->msg_id) != m_modifying_data_messages.end())
            data_msg = m_modifying_data_messages[msg->msg_id];
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
                          0,                                   // Complementary_value
                          (uint16_t)this->m_id);               // Owner
            msg.to.push_back((uint16_t)this->m_shared_memory_id);
            msg.source = Message::Source::SELF;
            if (buf.pushBack(msg, FRFCFS_State::NonReady))
                ((CacheDataHandler_COTS*)m_data_handler)->addressOfLinePendingWB(true, &evicted_address);

            for(int i = 0; i < (int)m_data_access_buffer.size(); )
            {
                if(getAddressKey(m_data_access_buffer[i].addr) == getAddressKey(evicted_address))
                {
                    auto action = m_data_access_action[m_data_access_buffer[i].msg_id];
                    action_functions[action.type](action.data);

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
            this->m_data_handler->modifyData(address, data, size, true);
            for(int i = 0; i < m_children_controllers.size(); i++)
                m_children_controllers[i]->initialize_child(address, data, size);
        }
    }

    void CacheController::initialize_child(uint64_t address, const uint8_t* data, int size) //for Initializable
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
            if(cache_line.owner_id == this->m_id || cache_line.owner_id == -1)
                memcpy(data, cache_line.m_data, cache_line.m_block_size);
            else
                m_children_controllers[cache_line.owner_id]->read(address, data);    
        }
    }
}