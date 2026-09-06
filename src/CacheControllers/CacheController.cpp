/*
 * File  :      CacheController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../../header/CacheControllers/CacheController.h"
#include "../../header/Protocols/MSIProtocol.h"   // REQUEST_TYPE_DATAREADY marker
#include "../../header/Protocols/TraceTransition.h"

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

        // Optional MSHR depth (max concurrent outstanding misses). Read from
        // config when present, otherwise a finite realistic default. -1 = off.
        m_num_mshr = DEFAULT_NUM_MSHR;
        if (parameters.find(STRINGIFY(num_mshr)) != parameters.end())
            m_num_mshr = std::get<int>(parameters.at(STRINGIFY(num_mshr)).value);

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
                                                                 processing_queue_size,
                                                                 ~(uint64_t)(m_data_handler->getBlockSize() - 1),
                                                                 /*readiness_state_only=*/true);

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
        action_functions[ControllerAction::Type::SEND_INV_MSG] = [&](void* ptr) {sendInvalidationMessage(ptr);};
        action_functions[ControllerAction::Type::START_READ] = [&](void* ptr) {startTimedRead(ptr);};
        action_functions[ControllerAction::Type::EMIT_DATAREADY] = [&](void* ptr) {emitDataReady(ptr);};
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

    // Owner data-forward transient (M_dS/M_dI): occupy the bank for the access latency and defer
    // a completion entry into the SHARED data-access buffer. It is serviced by the arbiter after
    // >=L cycles (processDataArrayBuffer), or force-drained on eviction (checkReplacements) with
    // the line living in the write buffer -- both existing paths. Either way it runs emitDataReady.
    void CacheController::startTimedRead(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        if (octopus::traceHit(msg->addr, m_data_handler->getBlockSize()))
            std::cout << "[TR cyc=" << m_cache_cycle << " L1  id=" << m_id << " a=0x" << std::hex << msg->addr
                      << std::dec << " STARTREAD readyAt=" << (m_cache_cycle + m_data_handler->getDataAccessLatency()) << "]" << std::endl;
        m_data_handler->markBusy(); // bank occupied for the access latency
        m_data_access_buffer.push_back(*msg);
        m_data_access_action[msg->msg_id] = ControllerAction{.type = ControllerAction::Type::EMIT_DATAREADY,
                                                             .data = data_ptr};
        // data_ptr is retained by m_data_access_action; freed by emitDataReady on completion.
    }

    // Completion of a deferred data-forward read: inject a self DataArrayReady message that
    // drives the transient (M_dS/M_dI) to its final state and forwards the data. No data is
    // attached -- the FSM's Data2Req/Data2Both reads the (still-valid, possibly write-buffer)
    // line itself; attaching data here would wrongly re-fill the line in updateCacheLine.
    void CacheController::emitDataReady(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        Message done(IdGenerator::nextReqId(), msg->addr, m_cache_cycle,
                     (uint16_t)MSIProtocol::REQUEST_TYPE_DATAREADY, (uint16_t)this->m_id);
        done.source = Message::Source::SELF;

        if (octopus::traceHit(msg->addr, m_data_handler->getBlockSize()))
            std::cout << "[TR cyc=" << m_cache_cycle << " L1  id=" << m_id << " a=0x" << std::hex << msg->addr
                      << std::dec << " EMIT-DATAREADY (completion)]" << std::endl;

        // Process the completion synchronously through the FSM (M_dS/M_dI + DataArrayReady ->
        // Data2Both/S or Data2Req/I) rather than queuing it. This forwards the data at the very
        // moment the read completes -- crucially, when this runs from the eviction drain the
        // line is still present (in the write buffer), so the forward reads it before the
        // eviction's write-back can clear it. "All pending work handled at eviction."
        std::vector<ControllerAction> actions = m_protocol->processRequest(done, dprint);
        for (ControllerAction action : actions)
            action_functions[action.type](action.data);

        // The transient just changed the line's state outside the processing queue
        // (e.g. M_dS -> S); a request stalled on that line is now serviceable, so
        // force a re-scan (the rescan-skip optimization can't see this change).
        m_processing_queue->markDirty();

        delete msg;
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

                auto _ait = m_data_access_action.find(selected_msg.msg_id);
                if (_ait == m_data_access_action.end())
                    return; // no action for this entry (defensive; should not happen)
                // Capture and ERASE the action BEFORE running it. The action (e.g. a data=NULL
                // write-back's performWriteBack) may re-defer itself via checkReadinessOfCache
                // when the bank is still busy, re-inserting this msg_id into the map. Erasing
                // after the call would then delete that fresh re-deferred entry, orphaning its
                // buffer element -> a later election finds no action and dereferences garbage.
                ControllerAction action = _ait->second;
                m_data_access_action.erase(_ait);
                action_functions[action.type](action.data);
                m_processing_queue->markDirty(); // deferred completion changed line state outside the queue
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

    // An owner completing its own upgrade (e.g. MOESI O/E -> M via Own_GetM) answers the
    // pending CPU request off its cache line -- a bank read. With data-array latency the
    // bank can be busy servicing an in-flight timed read (StartRead), so defer exactly as
    // hitAction/performWriteBack do; the deferred REMOVE_PENDING re-runs once the bank is
    // ready (processDataArrayBuffer) or on the eviction drain (checkReplacements). At
    // latency 0 the bank is always ready, so this is a no-op.
    void CacheController::removePendingAndRespond(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        if (msg->data == NULL &&
            !checkReadinessOfCache(*msg, ControllerAction::Type::REMOVE_PENDING, data_ptr))
                return;

        BaseController::removePendingAndRespond(data_ptr);
    }

    void CacheController::sendInvalidationMessage(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        msg->cycle = this->m_cache_cycle;

        // Back-invalidation travels on the TripleBus service channel: pushMessage
        // with SERVICE_REQUEST -> service bus broadcasts it to every interface, so
        // all L1 sharers see the INV (Invalidation) and the LLC receives its own
        // copy back (Own_Invalidation) to complete the eviction (WriteBack/N).
        if (!m_lower_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::SERVICE_REQUEST))
        {
            cout << "CacheController(id = " << this->m_id << "): Cannot insert INV into lower interface FIFO" << endl;
            exit(0);
        }

        delete msg;
    }

    void CacheController::performWriteBack(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        auto saved_it = this->m_saved_requests_for_wb.find(this->getAddressKey(msg->addr));
        if (saved_it != this->m_saved_requests_for_wb.end() && !saved_it->second.empty())
        {
            // Forward the data to EVERY pending sharer that requested while we were a
            // transient. The first becomes the primary destination (owner/msg_id); the
            // rest are added to `to` so the response bus delivers one copy to each. A
            // receiver classifies any inbound data as OwnData and matches it to its own
            // pending request by address, so a single fan-out message serves them all.
            std::vector<Message> &saved = saved_it->second;
            msg->owner = saved.front().owner;
            msg->msg_id = saved.front().msg_id;
            for (size_t i = 1; i < saved.size(); i++)
                msg->to.push_back(saved[i].owner);
            this->m_saved_requests_for_wb.erase(saved_it);
        }

        if(msg->data == NULL &&
           !checkReadinessOfCache(*msg, ControllerAction::Type::WRITE_BACK, data_ptr))
                return;

        BaseController::performWriteBack(data_ptr);
    }

    // Return the pending refill (deferred WRITE_CACHE_LINE_DATA) for `address` if one is
    // queued in the data-access buffer -- the just-arrived block that has not been written
    // to the array yet. NULL if there is none. (Used by the directory controller's forward.)
    Message *CacheController::getPendingFillData(uint64_t address)
    {
        for (const Message &m : m_data_access_buffer)
        {
            if (getAddressKey(m.addr) != getAddressKey(address))
                continue;
            auto it = m_data_access_action.find(m.msg_id);
            if (it != m_data_access_action.end() &&
                it->second.type == ControllerAction::Type::WRITE_CACHE_LINE_DATA)
            {
                Message *fill = (Message *)it->second.data;
                if (fill != NULL && fill->data != NULL)
                    return fill;
            }
        }
        return NULL;
    }

    void CacheController::dumpDeadlockState()
    {
        BaseController::dumpDeadlockState();
        std::cout << "   dataAccessBuf=" << m_data_access_buffer.size();
        for (const Message &m : m_data_access_buffer)
        {
            auto it = m_data_access_action.find(m.msg_id);
            int atype = (it != m_data_access_action.end()) ? (int)it->second.type : -1;
            GenericCacheLine cl; bool ok = m_data_handler->readLineBits(m.addr, &cl);
            std::cout << " {a=0x" << std::hex << m.addr << std::dec << " cv=" << m.complementary_value
                      << " actType=" << atype << " fsmSt=" << (ok ? cl.state : -1)
                      << " rdy=" << m_data_handler->isReady(m.addr) << "}";
        }
        std::cout << std::endl;
    }

    void CacheController::dropPendingFills(uint64_t address)
    {
        for (int i = 0; i < (int)m_data_access_buffer.size(); )
        {
            auto it = m_data_access_action.find(m_data_access_buffer[i].msg_id);
            if (it != m_data_access_action.end() &&
                it->second.type == ControllerAction::Type::WRITE_CACHE_LINE_DATA &&
                getAddressKey(m_data_access_buffer[i].addr) == getAddressKey(address))
            {
                // Free the deferred fill's data_ptr (placement-new Message + GenericCacheLine).
                void *dp = it->second.data;
                Message *m = (Message *)dp;
                GenericCacheLine *cl = (GenericCacheLine *)((uint8_t *)dp + sizeof(Message));
                cl->~GenericCacheLine();
                m->~Message();
                delete[] (uint8_t *)dp;

                m_data_access_action.erase(it);
                m_data_access_buffer.erase(m_data_access_buffer.begin() + i);
            }
            else
                i++;
        }
    }

    void CacheController::updateCacheLine(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        GenericCacheLine *cache_line = (GenericCacheLine *)((uint8_t *)data_ptr + sizeof(Message));

        // A coherence action invalidating this line makes any pending fill for it moot.
        if (!cache_line->valid)
            dropPendingFills(msg->addr);

        bool has_data = (msg->data != NULL);
        if (!has_data || !cache_line->valid || msg->data_size < m_data_handler->getBlockSize())
        {
            if (!m_data_handler->updateLineBits(msg->addr, cache_line))
                ((CacheDataHandler_COTS*)m_data_handler)->writeLine2MSHR(msg->addr, cache_line);//ToDo: Should be move to CacheDataHandler_COTS

            // If this (non-data) transition stabilizes the line and its data has been
            // waiting in the MSHR through the transient, do the single array write now.
            if (cache_line->valid && m_protocol->isStable(cache_line->state))
                ((CacheDataHandler_COTS*)m_data_handler)->promoteFromMSHR(msg->addr);
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

        // Land the just-arrived block in the MSHR (fill buffer). This makes the data
        // available immediately -- the core's pending hit and any forward are served
        // straight from the buffer, with no array access.
        bool inMSHR = ((CacheDataHandler_COTS*)m_data_handler)->fillMSHRData(msg->addr, msg->data);

        // While the line is still transient, the MSHR is its data home: keep the block
        // there and defer the single array write to stabilization (see updateCacheLine).
        // This avoids write-then-read-back, and a block acquired only to be forwarded on
        // (e.g. IM_a -> ... -> I) never touches the array at all.
        if (inMSHR && !m_protocol->isStable(cache_line->state))
        {
            cache_line->~GenericCacheLine();
            msg->~Message();
            delete[] (uint8_t *)data_ptr;
            return;
        }

        // Stable arrival, or a line already resident in a bank way: write the array now
        // (latency-gated for bank lines; immediate promotion for a buffered MSHR block).
        if(!checkReadinessOfCache(*msg, ControllerAction::Type::WRITE_CACHE_LINE_DATA, data_ptr))
            return;

        if (!m_data_handler->updateLineData(msg->addr, msg->data))
        {
            GenericCacheLine _cl; m_data_handler->readLineBits(msg->addr, &_cl);
            cout << "CacheController: update data of an unfound line [id=" << m_id
                 << " a=0x" << std::hex << msg->addr << std::dec << " st=" << _cl.state
                 << " cv=" << msg->complementary_value << " src=" << (int)msg->source << "]" << endl;
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
        // A self-generated PutM (Own_PutM) saved for writeback must target the LLC
        // (shared memory), not us -- otherwise performWriteBack's fan-out lists this
        // core in `to` and delivers the writeback data back to ourselves (I + OwnData
        // -> Fault). Mirrors the owner==m_id -> m_shared_memory_id redirect that the
        // direct data-send path (BaseController::performWriteBack) already applies.
        if (msg->owner == this->m_id)
            msg->owner = this->m_shared_memory_id;
        this->m_saved_requests_for_wb[this->getAddressKey(msg->addr)].push_back(*msg);

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

    bool CacheController::canAdmitRequest(Message &msg)
    {
        // Only a brand-new demand request can open a fresh outstanding miss (and
        // thus consume an MSHR slot). Responses, snoops, write-backs and self-
        // generated replacement messages must always proceed -- never back-pressure
        // a response, or a full MSHR/queue starves the traffic that would drain it.
        // Keyed on message kind, not interface: an L1 write-back is a response that
        // reaches the LLC on the lower interface.
        if (!msg.isDemandRequest())
            return true;

        CacheDataHandler_COTS *cots = (CacheDataHandler_COTS *)m_data_handler;

        // Already resident, or already tracked in MSHR/PWB -> a hit or a
        // coalesced access; no new MSHR entry is needed.
        if (cots->isAddressTracked(msg.addr))
            return true;

        // Coalesces with an already-outstanding miss to the same block.
        if (m_pending_requests.find(getAddressKey(msg.addr)) != m_pending_requests.end())
            return true;

        // Brand-new miss: require a free MSHR slot (bound on the number of
        // concurrent outstanding misses). The PWB is now enforced strictly at
        // fill time (above), so misses are no longer throttled on PWB here.
        if (m_num_mshr >= 0 && (int)m_pending_requests.size() >= m_num_mshr)
            return false;

        return true;
    }

    bool CacheController::checkReadinessOfCache(Message &msg, ControllerAction::Type type, void *data_ptr)
    {
        if(!m_data_handler->isReady(msg.addr))
        {
            if (m_data_access_action.find(msg.msg_id) != m_data_access_action.end())
                std::cout << "[DAB-DUP id=" << m_id << " msg_id=" << msg.msg_id
                          << " a=0x" << std::hex << msg.addr << std::dec
                          << " newtype=" << (int)type << " cv=" << msg.complementary_value
                          << " src=" << (int)msg.source << "]" << std::endl;
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
            if (octopus::traceHit(evicted_address, m_data_handler->getBlockSize()))
            {
                GenericCacheLine _cl; m_data_handler->readLineBits(evicted_address, &_cl);
                std::cout << "[TR cyc=" << m_cache_cycle << " L1  id=" << m_id << " a=0x" << std::hex
                          << evicted_address << std::dec << " EVICT (victim in PWB) st=" << _cl.state << "]" << std::endl;
            }
            msg = Message(IdGenerator::nextReqId(),            // Id
                          evicted_address,                     // Addr
                          m_cache_cycle,                       // Cycle
                          0,                                   // Complementary_value
                          (uint16_t)this->m_id);               // Owner
            msg.to.push_back((uint16_t)this->m_shared_memory_id);
            msg.source = Message::Source::SELF;
            // force=true: a write-back must be admitted to drain the PWB. It is
            // maintenance traffic, not new demand, so it bypasses the queue cap
            // (which bounds demand admission only). Bounded upstream by pwb_size.
            if (buf.pushBack(msg, FRFCFS_State::NonReady, /*force=*/true))
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