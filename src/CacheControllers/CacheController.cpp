/*
 * File  :      CacheController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 23, 2021
 */

#include "../../header/CacheControllers/CacheController.h"
#include "../../header/ExternalCPU.h"

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
        if (arbiter_candidates_ids != NULL)
            m_arbiter_candidates = *arbiter_candidates_ids;
        
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
    }

    CacheController::~CacheController()
    {
        delete m_protocol;
        delete m_data_handler;
    }

    void CacheController::cycleProcess()
    {
        m_data_handler->updateCycle(m_cache_cycle);
        if (pipelinedArray())
            this->pipelineStep();
        else
            this->processDataArrayBuffer();
        this->processLogic(); // Call cache controller

        m_cache_cycle++;
    }

    void CacheController::processDataArrayBuffer()
    {
        // Occupancy model, level 2: when the array is free, one waiting
        // access runs, chosen by the configured arbiter (keyed on the
        // requesting core) or oldest first; its own array access closes the
        // array again. A whole deferred message runs its FSM now, so its
        // state change and its bytes land together; a parked single action
        // runs its byte phase now.
        if (!m_data_handler->isReady())
            return;
        auto it = arrayElect();
        if (it == m_array_ops.end())
            return;
        DataArrayOp op = std::move(*it);
        m_array_ops.erase(it);
        if (op.kind == DataArrayOp::ACTIONS)
            Logger::getLogger()->updateRequest(op.msg.msg_id, Logger::EntryId::CACHE_CHECKPOINT);
        pipelineFire(op);
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

    void CacheController::updateCacheLine(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        GenericCacheLine *cache_line = (GenericCacheLine *)((uint8_t *)data_ptr + sizeof(Message));

        // L1 only: the core loses its readable copy of this line (remote
        // ownership request, LLC invalidation, or replacement). The stored
        // bits are still the old ones at this point, whichever branch below
        // writes the new ones.
        if (m_cpu_port != NULL)
        {
            GenericCacheLine old_line;
            if (m_data_handler->readLineBits(msg->addr, &old_line) &&
                m_protocol->isReadableState(old_line.state) &&
                !m_protocol->isReadableState(cache_line->state))
                m_cpu_port->invalidate(msg->addr);
        }

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

    bool CacheController::demandAdmissionBlocked(int outstanding) const
    {
        if (BaseController::demandAdmissionBlocked(outstanding))
            return true;
        // MSHR full: a brand-new miss could not be admitted (canAdmitRequest).
        if (m_num_mshr >= 0 && (int)m_pending_requests.size() >= m_num_mshr)
            return true;
        // Write-back buffer full: a fill that must evict would stall.
        CacheDataHandler_COTS *cots = (CacheDataHandler_COTS *)m_data_handler;
        if (!cots->pwbHasSpace())
            return true;
        return false;
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
        if (pipelinedArray())
        {
            // Completing an op: its array accesses are the op itself.
            if (m_pipe_firing)
                return true;
            // A line held in the MSHR or the write-back buffer is not in the
            // array: it is read or written in place, as the occupancy model
            // does through isReady(address). Parking such an access raced
            // with the buffer entry's release: an eviction's WriteBack reads
            // the buffered line and the state change that follows it (to N)
            // erases the entry at once, so the parked read found nothing.
            CacheDataHandler::LineLocation loc = m_data_handler->lineLocation(msg.addr);
            if (loc == CacheDataHandler::LineLocation::MSHR || loc == CacheDataHandler::LineLocation::PWB)
                return true;
        }
        else if (m_data_handler->isReady(msg.addr))
            return true;    // array free, or the line is in a side buffer: run inline

        // The byte phase waits for the array: one more access on the level-2
        // list, scheduled with everything else that needs the array.
        DataArrayOp op;
        op.kind = DataArrayOp::ACTIONS;
        op.msg = msg;
        op.actions.push_back(ControllerAction{.type = type, .data = data_ptr});
        arrayEnqueue(std::move(op));
        return false;
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
            // force=true: a write-back must be admitted to drain the PWB. It is
            // maintenance traffic, not new demand, so it bypasses the queue cap
            // (which bounds demand admission only). Bounded upstream by pwb_size.
            if (buf.pushBack(msg, FRFCFS_State::NonReady, /*force=*/true))
                ((CacheDataHandler_COTS*)m_data_handler)->addressOfLinePendingWB(true, &evicted_address);

            // The victim's bytes have moved to the write-back buffer: every
            // access still waiting on that line completes now, before the
            // write-back can release the buffered copy (both models).
            pipelineFlushLine(evicted_address);
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

    // ------------------------------------------------------------------
    // Pipelined data array
    // ------------------------------------------------------------------

    bool CacheController::pipelinedArray() const
    {
        return m_data_handler->isPipelined() && m_data_handler->getDataAccessLatency() > 0;
    }

    void CacheController::arrayEnqueue(DataArrayOp &&op)
    {
        m_pipe_ops++;
        op.op_id = ++m_array_op_seq;
        m_array_ops.push_back(std::move(op));
        // Pipelined: an access may start the cycle it arrives, if a port is
        // left and the policy picks it (oldest first picks it when nothing
        // older waits; an arbiter applies its own rule).
        if (pipelinedArray())
            arrayAdmit();
    }

    bool CacheController::arbitrated(int owner) const
    {
        for (int id : m_arbiter_candidates)
            if (id == owner)
                return true;
        return false;
    }

    std::deque<CacheController::DataArrayOp>::iterator CacheController::arrayElect()
    {
        // No arbiter (the L1 default): oldest waiting access.
        if (m_data_access_arbiter == NULL)
        {
            for (auto it = m_array_ops.begin(); it != m_array_ops.end(); ++it)
                if (!it->admitted)
                    return it;
            return m_array_ops.end();
        }
        // The arbiter elects among messages by requesting core (owner) and
        // removes the winner from the vector it is given: hand it copies of
        // the waiting accesses' messages with msg_id replaced by the op id,
        // then map the winner back. Accesses whose owner is not one of its
        // candidates (a controller's own maintenance traffic) are outside the
        // policy and go oldest first when the arbiter elects nothing.
        vector<Message> candidates;
        for (const DataArrayOp &op : m_array_ops)
            if (!op.admitted && arbitrated(op.msg.owner))
            {
                Message m(op.msg);
                m.msg_id = op.op_id;
                candidates.push_back(m);
            }
        if (!candidates.empty())
        {
            vector<vector<Message> *> wrap;
            wrap.push_back(&candidates);
            Message elected;
            if (m_data_access_arbiter->elect(m_cache_cycle, wrap, &elected))
                for (auto it = m_array_ops.begin(); it != m_array_ops.end(); ++it)
                    if (!it->admitted && it->op_id == elected.msg_id)
                        return it;
        }
        for (auto it = m_array_ops.begin(); it != m_array_ops.end(); ++it)
            if (!it->admitted && !arbitrated(it->msg.owner))
                return it;
        return m_array_ops.end();
    }

    void CacheController::arrayAdmit()
    {
        while (m_array_admitted_this_cycle < m_data_handler->getDataArrayPorts())
        {
            auto it = arrayElect();
            if (it == m_array_ops.end())
                return;
            it->admitted = true;
            it->ready_cycle = m_cache_cycle + m_data_handler->getDataAccessLatency();
            m_array_admitted_this_cycle++;
        }
    }

    void CacheController::pipelineFire(DataArrayOp &op)
    {
        bool was_firing = m_pipe_firing;
        m_pipe_firing = true;

        if (op.kind == DataArrayOp::MESSAGE)
        {
            // The bytes have been written: apply the FSM event now, against
            // the line's current state, exactly as processLogic would have.
            if (op.msg.source == Message::Source::LOWER_INTERCONNECT)
                Logger::getLogger()->updateRequest(op.msg.msg_id, Logger::EntryId::CACHE_CHECKPOINT);
            vector<ControllerAction> actions = m_protocol->processRequest(op.msg, dprint);
            for (ControllerAction action : actions)
                action_functions[action.type](action.data);
        }
        else
        {
            for (ControllerAction action : op.actions)
                action_functions[action.type](action.data);
        }

        m_pipe_firing = was_firing;
        // The line's state may have changed; messages stalled on it must be
        // rescanned (readiness is state-only and rescans only after a push/pop).
        m_processing_queue->markDirty();
    }

    void CacheController::pipelineStep()
    {
        // Pipelined model, level 2: admit up to the ports' worth of waiting
        // accesses by policy, then complete every admitted access whose
        // latency has elapsed. Completing one may enqueue another (a second
        // array access of the same action list), which invalidates deque
        // iterators, so rescan after each completion.
        m_array_admitted_this_cycle = 0;
        arrayAdmit();
        for (bool fired = true; fired; )
        {
            fired = false;
            for (auto it = m_array_ops.begin(); it != m_array_ops.end(); ++it)
                if (it->admitted && it->ready_cycle <= m_cache_cycle)
                {
                    DataArrayOp op = std::move(*it);
                    m_array_ops.erase(it);
                    pipelineFire(op);
                    fired = true;
                    break;
                }
        }
    }

    void CacheController::pipelineFlushLine(uint64_t address)
    {
        // The line is leaving the array: complete every access waiting on it
        // now, oldest first, before its bytes move to the write-back buffer.
        uint64_t key = getAddressKey(address);
        for (bool fired = true; fired; )
        {
            fired = false;
            for (auto it = m_array_ops.begin(); it != m_array_ops.end(); ++it)
                if (getAddressKey(it->msg.addr) == key)
                {
                    DataArrayOp op = std::move(*it);
                    m_array_ops.erase(it);
                    m_pipe_early_fires++;
                    pipelineFire(op);
                    fired = true;
                    break;
                }
        }
    }




    bool CacheController::messageTouchesArray(const Message &msg)
    {
        // Occupancy model: only the transitions the protocol reports (the
        // snoop L1 protocols' Hit / Data2Req / Data2Both); a data-carrying
        // message keeps upstream's handling so the lab presets are unchanged.
        // Pipelined model: those, plus any bytes arriving from below or from
        // a peer, which are array writes whatever the table says.
        if (m_protocol->needsDataArray(msg))
            return true;
        if (!pipelinedArray())
            return false;
        bool cpu_demand = msg.source == Message::Source::LOWER_INTERCONNECT &&
                          (msg.complementary_value == 0 || msg.complementary_value == 1);
        return msg.data != NULL && !cpu_demand;
    }

    bool CacheController::deferForDataArray(Message &msg)
    {
        if (!pipelinedArray())
        {
            // Occupancy model with a non-zero latency: a message whose
            // transition touches the array waits, whole, until the array is
            // free, then runs inline so its array access and its state change
            // happen together. Without this the state change ran at once and
            // the parked read later found the line invalidated or evicted.
            // Only transitions the protocol reports as array accesses (snoop
            // L1 protocols: Hit, Data2Req, Data2Both; LLC protocols: SendData,
            // SendExeclusiveData, SaveData). A data-carrying message whose
            // protocol reports nothing keeps upstream's handling (state now,
            // bytes parked on the same level-2 list).
            if (m_data_handler->getDataAccessLatency() == 0 || m_data_handler->isReady())
                return false;
            if (!m_protocol->needsDataArray(msg))
                return false;
            DataArrayOp op;
            op.kind = DataArrayOp::MESSAGE;
            op.msg = msg;
            arrayEnqueue(std::move(op));
            return true;
        }
        // Defer the whole message, and so the state change that comes with
        // it, when its transition touches the array: a hit, a snoop that is
        // answered with the line, a fill. The line keeps its current state
        // for the array latency, so a snoop that needs the bytes waits behind
        // the access that is producing them instead of taking the line away
        // first. Messages that carry bytes from below (fills, peer data) are
        // array writes whatever the table says; a CPU store's bytes are
        // written by its Hit, which the table reports.
        // A line in the write-back buffer has left the array for good: a
        // request served from it, its write-back trigger, or an owner's bytes
        // merged into it touch the buffer only. (A line in the MSHR is
        // different: the data message for it is the fill, an array write.)
        if (m_data_handler->lineLocation(msg.addr) == CacheDataHandler::LineLocation::PWB)
            return false;
        if (!messageTouchesArray(msg))
            return false;

        DataArrayOp op;
        op.kind = DataArrayOp::MESSAGE;
        op.msg = msg;
        arrayEnqueue(std::move(op));
        return true;
    }

    void CacheController::removePendingAndRespond(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        // A response built from the resident line needs the data array, like
        // a hit. Without this guard the base version reads inline and, in the
        // occupancy model, aborts if the array happens to be busy.
        if (msg->data == NULL &&
            !checkReadinessOfCache(*msg, ControllerAction::Type::REMOVE_PENDING, data_ptr))
            return;
        BaseController::removePendingAndRespond(data_ptr);
    }
}