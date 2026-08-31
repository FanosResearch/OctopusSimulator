/*
 * File  :      BaseController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sep 14, 2023
 */

#include "../../header/CacheControllers/BaseController.h"
#include "../../header/CacheDataHandler_COTS.h"
#include "../../header/Protocols/TraceTransition.h"

namespace octopus
{
    // --- SCRATCH: no-progress (deadlock) detector ---
    std::vector<BaseController*> BaseController::s_controllers;
    uint64_t BaseController::s_last_progress_cycle = 0;
    uint64_t BaseController::s_max_cycle = 0;
    bool BaseController::s_dumped = false;

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

        s_controllers.push_back(this); // SCRATCH: deadlock detector registry
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

        if (m_cache_cycle > s_max_cycle) s_max_cycle = m_cache_cycle; // SCRATCH
        checkGlobalStall();                                          // SCRATCH

        m_cache_cycle++;
    }

    // SCRATCH: fire once when no demand request has retired for a long window,
    // then dump every controller's queue head / MSHR / PWB and exit.
    void BaseController::checkGlobalStall()
    {
        if (s_dumped) return;
        if (s_max_cycle > s_last_progress_cycle + 100000)
        {
            s_dumped = true;
            std::cout << "\n[DEADLOCK-DUMP] no CPU response for >100000 cyc"
                      << " (max_cyc=" << s_max_cycle
                      << " last_progress=" << s_last_progress_cycle << ")" << std::endl;
            for (BaseController *c : s_controllers) c->dumpDeadlockState();
            exit(0);
        }
    }

    void BaseController::dumpDeadlockState()
    {
        int qs = m_processing_queue->size();
        std::cout << "[DL id=" << m_id << " qsize=" << qs
                  << " mshr=" << m_pending_requests.size() << "/" << mshrLimit() << "]" << std::endl;
        int lim = qs < 8 ? qs : 8;
        for (int i = 0; i < lim; i++)
        {
            Message msg; FRFCFS_State st;
            if (!m_processing_queue->peekAt(i, &msg, &st)) break;
            GenericCacheLine cl; bool ok = m_data_handler->readLineBits(msg.addr, &cl);
            bool admit = canAdmitRequest(msg);
            int wi = ((CacheDataHandler_COTS*)m_data_handler)->whereIs(msg.addr);
            int live = (int)m_protocol->getRequestState(msg, st); // recompute readiness now
            std::cout << "   q[" << i << "] a=0x" << std::hex << msg.addr << std::dec
                      << " src=" << (int)msg.source << " cv=" << msg.complementary_value
                      << " demand=" << msg.isDemandRequest()
                      << " fsmSt=" << (ok ? cl.state : -1) << " qSt=" << (int)st
                      << " liveSt=" << live
                      << " admit=" << admit << " whereIs=" << wi << std::endl;
        }
        for (auto &kv : m_pending_requests)
        {
            GenericCacheLine cl; bool ok = m_data_handler->readLineBits(kv.first, &cl);
            int wi = ((CacheDataHandler_COTS*)m_data_handler)->whereIs(kv.first);
            std::cout << "   MSHR a=0x" << std::hex << kv.first << std::dec
                      << " nreq=" << kv.second.size()
                      << " fsmSt=" << (ok ? cl.state : -1)
                      << " rdy=" << m_data_handler->isReady(kv.first)
                      << " whereIs=" << wi;
            if (!kv.second.empty())
                std::cout << " reqSrc=" << (int)kv.second.front().source
                          << " reqCv=" << kv.second.front().complementary_value;
            std::cout << std::endl;
        }
        uint64_t wb = 0;
        bool haswb = ((CacheDataHandler_COTS*)m_data_handler)->addressOfLinePendingWB(false, &wb);
        std::cout << "   pendingWB=" << haswb;
        if (haswb) std::cout << " a=0x" << std::hex << wb << std::dec;
        std::cout << std::endl;
    }

    void BaseController::init()
    {
        m_protocol->initializeCacheStates(); // Initialized Cache Coherence Protocol
    }

    void BaseController::processLogic()
    {
        if (m_cache_cycle > s_max_cycle) s_max_cycle = m_cache_cycle; // SCRATCH
        checkGlobalStall();                                          // SCRATCH

        this->addRequests2ProcessingQueue(*m_processing_queue);

        while(true)
        {
            Message ready_msg;
            if (m_processing_queue->getFirstReady(&ready_msg) == false)
                return;

            if (!canAdmitRequest(ready_msg))
            {
                // Structural stall (e.g., MSHR/PWB full): put the request back
                // and retry next cycle. A slot is guaranteed to be free because
                // getFirstReady just removed this element.
                m_processing_queue->pushBack(ready_msg, FRFCFS_State::NonReady);
                return;
            }

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
            if (octopus::traceHit(msg.addr, m_data_handler->getBlockSize())
                || (m_id == 3 && m_cache_cycle >= 1195893 && m_cache_cycle <= 1195902))
                std::cout << "[PULL cyc=" << m_cache_cycle << " id=" << m_id << " UP  a=0x" << std::hex
                          << msg.addr << std::dec << " cv=" << msg.complementary_value
                          << " own=" << msg.owner << " d=" << (msg.data != NULL) << "]" << std::endl;
            msg.source = Message::Source::UPPER_INTERCONNECT;
            msg.order = m_arrival_seq++;
            if (buf.pushFront(msg))
                m_upper_interface->popFrontMessage();
        }

        if (m_lower_interface->peekMessage(&msg))
        {
            if (octopus::traceHit(msg.addr, m_data_handler->getBlockSize()))
                std::cout << "[PULL cyc=" << m_cache_cycle << " id=" << m_id << " LOW a=0x" << std::hex
                          << msg.addr << std::dec << " cv=" << msg.complementary_value
                          << " own=" << msg.owner << " d=" << (msg.data != NULL) << "]" << std::endl;
            msg.source = Message::Source::LOWER_INTERCONNECT;
            msg.order = m_arrival_seq++;
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
                    GenericCacheLine _cl; bool _f = m_data_handler->readLineBits(msg->addr, &_cl);
                    int _res = ((CacheDataHandler_COTS*)m_data_handler)->whereIs(msg->addr);
                    cout << "CacheController: Remove from pending without data"
                         << " [id=" << m_id << " a=0x" << std::hex << msg->addr << std::dec
                         << " bitsOK=" << _f << " whereIs=" << _res << " st=" << _cl.state
                         << " src=" << (int)msg->source << " owner=" << msg->owner
                         << " ready=" << m_data_handler->isReady(msg->addr) << "]" << endl;
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

        s_last_progress_cycle = m_cache_cycle; // SCRATCH: a demand retired
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
        }

        if (!m_lower_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::DATA_RESPONSE))
        {
            cout << "CacheController: Cannot insert the Msg into lower interface." << endl;
            exit(0);
        }

        s_last_progress_cycle = m_cache_cycle; // SCRATCH: a demand retired
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
            bool found = m_data_handler->readCacheLine(msg->addr, &cache_line);
            if (!found || cache_line.m_data == NULL)
            {
                GenericCacheLine _cl; bool _f = m_data_handler->readLineBits(msg->addr, &_cl);
                int _res = ((CacheDataHandler_COTS*)m_data_handler)->whereIs(msg->addr);
                std::cout << "[WB-NODATA id=" << m_id << " a=0x" << std::hex << msg->addr << std::dec
                          << " readOK=" << found << " bitsOK=" << _f << " whereIs=" << _res
                          << " st=" << _cl.state << " rdy=" << m_data_handler->isReady(msg->addr) << "]" << std::endl;
                exit(0);
            }
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