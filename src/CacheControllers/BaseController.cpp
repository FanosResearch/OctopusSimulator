/*
 * File  :      BaseController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sep 14, 2023
 */

#include "../../header/CacheControllers/BaseController.h"
#include "../../header/ExternalCPU.h"

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
        m_processing_queue_size = processing_queue_size;
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
                                                                 processing_queue_size,
                                                                 ~(uint64_t)(m_data_handler->getBlockSize() - 1),
                                                                 /*readiness_state_only=*/true);

        dprint = new DebugPrint(getSubMap(STRINGIFY(dprint)), name + std::to_string(m_id), parent_name + "." + name);

        action_functions.resize(ControllerAction::Type::MAX_ACTIONS_NUM);
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
            traceMsg("popped", ready_msg);

            if (!canAdmitRequest(ready_msg))
            {
                traceMsg("admit-fail", ready_msg);
                // Structural stall (e.g., MSHR/PWB full): put the request back
                // and retry next cycle. A slot is guaranteed to be free because
                // getFirstReady just removed this element.
                m_processing_queue->pushBack(ready_msg, FRFCFS_State::NonReady);
                return;
            }

            if (deferForDataArray(ready_msg))
                continue;

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
            traceMsg("intake-upper", msg);
            if (buf.pushFrontOrdered(msg))
                m_upper_interface->popFrontMessage();
        }

        if (m_lower_interface->peekMessage(&msg))
        {
            msg.source = Message::Source::LOWER_INTERCONNECT;
            traceMsg("intake-lower", msg);
            // Only demand requests are subject to the queue bound. Responses and
            // service traffic (data fills, write-backs, invalidations) must always
            // be admitted: a full queue of stalled demand requests would otherwise
            // reject the very response that would drain them, deadlocking. (At the
            // LLC, an L1 write-back is a response that arrives on the lower interface,
            // so route by message kind, not by interface.)
            if (buf.pushBack(msg, FRFCFS_State::NonReady, /*force=*/!msg.isDemandRequest()))
                m_lower_interface->popFrontMessage();
        }
    }

    void BaseController::dataArrayReadFailed(const char *where, const Message *msg)
    {
        // A response that needs the line's bytes found no readable line: the
        // line's state was changed (invalidated or evicted) before the array
        // access that should have preceded it ran. With a data latency of 0
        // the read runs inline first; with a latency it must be parked with
        // the state change, or this is what happens.
        m_data_read_failures++;
        // Standalone traces carry mock data, so a response without bytes was
        // always tolerated there; keep that. With an external core attached
        // the bytes will matter (Stage 4), so make it fatal.
        if (m_cpu_port == NULL)
            return;
        GenericCacheLine bits;
        bool have_bits = m_data_handler->readLineBits(msg->addr, &bits);
        cout << "CacheController(id = " << m_id << "): " << where
             << " needs the data of line 0x" << std::hex << msg->addr << std::dec
             << " but the array has no readable copy (state "
             << (have_bits ? bits.state : -1) << ", valid " << (have_bits ? bits.valid : false)
             << ", msg " << msg->msg_id << "). The state changed before the array read." << endl;
        exit(0);
    }

    bool BaseController::demandAdmissionBlocked(int outstanding) const
    {
        return m_processing_queue_size >= 0 && outstanding >= m_processing_queue_size;
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

        // An owner completing a store-upgrade (e.g. MOESI O->M) responds to its pending
        // CPU request off its own valid cache line: the triggering bus message (Own_GetM)
        // carries no data, so read the line here rather than faulting for lack of data.
        if (msg->data == NULL)
        {
            GenericCacheLine cache_line;
            if (m_data_handler->readCacheLine(msg->addr, &cache_line) && cache_line.m_data != NULL)
                msg->copy(cache_line.m_data);
            else
                dataArrayReadFailed("removePendingAndRespond", msg);
        }

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

                // Serialisation point of this CPU request.
                if (m_cpu_port != NULL)
                    m_cpu_port->commit(pending_messages.front().msg_id, pending_messages.front().addr);

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
            if (m_data_handler->readCacheLine(msg->addr, &cache_line) && cache_line.m_data != NULL)
                msg->copy(cache_line.m_data);
            else
                dataArrayReadFailed("hitAction", msg);
        }

        // Serialisation point of this CPU request.
        if (m_cpu_port != NULL)
            m_cpu_port->commit(msg->msg_id, msg->addr);

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
            if (m_data_handler->readCacheLine(msg->addr, &cache_line) && cache_line.m_data != NULL)
                msg->copy(cache_line.m_data);
            else
                dataArrayReadFailed("performWriteBack", msg);
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