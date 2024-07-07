/*
 * File  :      BaseController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sep 14, 2023
 */

#include "../../header/CacheControllers/BaseController.h"

namespace octopus
{
    // private controller constructor
    BaseController::BaseController(ParametersMap map, CommunicationInterface *upper_interface, 
                                   CommunicationInterface *lower_interface, string pname, string config_path, string name)
    : ClockedObj(0), Configurable(map, config_path, name, pname)
    {
        m_cache_cycle = 1;

        //Parameters initialization
        m_id = std::get<int>(parameters.at(STRINGIFY(m_id)).value);
        m_shared_memory_id = std::get<int>(parameters.at(STRINGIFY(m_shared_memory_id)).value);
        m_clk_period = std::get<int>(parameters.at(STRINGIFY(m_clk_period)).value);
        int processing_queue_size = std::get<int>(parameters.at(STRINGIFY(processing_queue_size)).value);
        string protocol_type = std::get<string>(parameters.at(STRINGIFY(protocol_type)).value);
        string fsm_filename = std::get<string>(parameters.at(STRINGIFY(fsm_filename)).value);
        string fsm_path = string(FSM_PATH) + fsm_filename + ".csv";

        //Constructor
        m_upper_interface = upper_interface;
        m_lower_interface = lower_interface;
        
        m_data_handler = new CacheDataHandler(getSubMap(STRINGIFY(m_data_handler)), parent_name + "." + name);

        m_protocol = Protocols::getNewProtocol(protocol_type, m_data_handler, fsm_path, m_id, m_shared_memory_id);

        m_processing_queue =
            new FRFCFS_Buffer<Message, CoherenceProtocolHandler>(&CoherenceProtocolHandler::getRequestState,
                                                                 m_protocol,
                                                                 processing_queue_size);
        
        dprint = new DebugPrint(getSubMap(STRINGIFY(dprint)), name + std::to_string(m_id), parent_name + "." + name);

        action_functions.reserve(ControllerAction::Type::MAX_ACTIONS_NUM);
        action_functions[ControllerAction::Type::REMOVE_PENDING] = [&](void* ptr) {this->removePendingAndRespond(ptr);};
        action_functions[ControllerAction::Type::HIT_Action] = [&](void* ptr) {hitAction(ptr);};
        action_functions[ControllerAction::Type::ADD_PENDING] = [&](void* ptr) {addtoPendingRequests(ptr);};
        action_functions[ControllerAction::Type::SEND_BUS_MSG] = [&](void* ptr) {sendBusRequest(ptr);};
        action_functions[ControllerAction::Type::WRITE_BACK] = [&](void* ptr) {performWriteBack(ptr);};
        action_functions[ControllerAction::Type::UPDATE_CACHE_LINE] = [&](void* ptr) {updateCacheLine(ptr);};
    }

    BaseController::~BaseController()
    {
        delete m_protocol;
        delete m_data_handler;
    }

    void BaseController::cycleProcess()
    {
        m_data_handler->updateCycle(m_cache_cycle);
        this->processLogic(); // Call cache controller

        m_cache_cycle++;
    }

    void BaseController::init()
    {
        m_protocol->initializeCacheStates(); // Initialized Cache Coherence Protocol
    }

    void BaseController::processLogic()
    {
        this->addRequests2ProcessingQueue(*m_processing_queue);

        while(true)
        {
            Message ready_msg;
            if (m_processing_queue->getFirstReady(&ready_msg) == false)
                return;

            if(ready_msg.source == Message::Source::LOWER_INTERCONNECT)
                Logger::getLogger()->updateRequest(ready_msg.msg_id, Logger::EntryId::CACHE_CHECKPOINT);

            // if(ready_msg.source == Message::Source::SELF)
            //     dprint->print(NULL, "Ready Message for Replacement");
            // else
            //     dprint->print(&ready_msg, "Ready Message from %s", ready_msg.source == Message::Source::LOWER_INTERCONNECT? 
            //              "Lower Interface" : "Upper Interface");

            vector<ControllerAction> actions = m_protocol->processRequest(ready_msg, dprint);

            for (ControllerAction action : actions)
                action_functions[action.type](action.data);
        }
    }

    void BaseController::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf)
    {
        Message msg;

        if (m_upper_interface->peekMessage(&msg))
        {
            msg.source = Message::Source::UPPER_INTERCONNECT;
            if (buf.pushFront(msg))
                m_upper_interface->popFrontMessage();
        }

        if (m_lower_interface->peekMessage(&msg))
        {
            msg.source = Message::Source::LOWER_INTERCONNECT;
            if (buf.pushBack(msg, FRFCFS_State::NonReady))
                m_lower_interface->popFrontMessage();
        }
    }

    uint64_t BaseController::getAddressKey(uint64_t addr)
    {
        return (addr & ~uint64_t(m_data_handler->getBlockSize() - 1));
    }

    void BaseController::addtoPendingRequests(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        this->m_pending_requests[this->getAddressKey(msg->addr)].push_back(*msg);

        delete msg;
    }

    void BaseController::removePendingAndRespond(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        if (m_pending_requests.find(getAddressKey(msg->addr)) != m_pending_requests.end())
        {
            vector<Message> pending_messages = this->m_pending_requests[this->getAddressKey(msg->addr)];
            
            while (!pending_messages.empty())
            {
                if (msg->data != NULL)
                {
                    pending_messages.front().complementary_value = msg->complementary_value;
                    pending_messages.front().to = msg->to;
                    pending_messages.front().copy(msg->data);
                }
                else
                { 
                    cout << "CacheController: Remove from pending without data" << endl;
                    exit(0);
                }

                if (!m_lower_interface->pushMessage(pending_messages.front(), this->m_cache_cycle, MessageType::DATA_RESPONSE))
                {
                    cout << "CacheController: Cannot insert the Msg into lower interface." << endl;
                    exit(0);
                }
                pending_messages.erase(pending_messages.begin());
            }
            this->m_pending_requests.erase(this->getAddressKey(msg->addr));
        }
        else
        { 
            cout << "CacheController: Request is not found in the pending buffer." << endl;
            exit(0);
        }

        delete msg;
    }

    void BaseController::hitAction(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        if (msg->data == NULL)
        {
            GenericCacheLine cache_line;
            m_data_handler->readCacheLine(msg->addr, &cache_line);
            msg->copy(cache_line.m_data);
        }

        if (!m_lower_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::DATA_RESPONSE))
        {
            cout << "CacheController: Cannot insert the Msg into lower interface." << endl;
            exit(0);
        }

        delete msg;
    }

    void BaseController::sendBusRequest(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        msg->cycle = this->m_cache_cycle;

        if (!m_upper_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::REQUEST))
        {
            cout << "CacheController(id = " << this->m_id << "): Cannot insert the Msg into the upper interface FIFO, FIFO is Full" << endl;
            exit(0);
        }

        delete msg;
    }

    void BaseController::performWriteBack(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        if(msg->data == NULL)
        {
            GenericCacheLine cache_line;
            m_data_handler->readCacheLine(msg->addr, &cache_line);
            msg->copy(cache_line.m_data);
        }

        if (msg->owner == this->m_id)
            msg->to.push_back(this->m_shared_memory_id);
        else
            msg->to.push_back(msg->owner);

        if (!m_upper_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::DATA_RESPONSE))
        {
            cout << "CacheController: Cannot insert the Msg into BusTxResp FIFO, FIFO is Full" << endl;
            exit(0);
        }

        delete msg;
    }

    void BaseController::updateCacheLine(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        GenericCacheLine *cache_line = (GenericCacheLine *)((uint8_t *)data_ptr + sizeof(Message));
        
        m_data_handler->updateLineBits(msg->addr, cache_line);
    
        if (msg->data != NULL)
        {
            if (!m_data_handler->updateLineData(msg->addr, msg->data))
            {
                cout << "CacheController: update data of an unfound line" << endl;
                exit(0);
            }
        }

        cache_line->~GenericCacheLine();    //explicit call for the destructor due to the use of placement new
        msg->~Message();                    //explicit call for the destructor due to the use of placement new

        delete[] (uint8_t *)data_ptr;
    }
}