/*
 * File  :      PredictableDirectory.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 3, 2023
 */

#include "../../header/CacheControllers/PredictableDirectory.h"

namespace ns3
{
    PredictableDirectory::PredictableDirectory(ParametersMap map, CommunicationInterface *upper_interface, CommunicationInterface *lower_interface,
                                                       string pname, string config_path, string name) 
    : CacheControllerDirectory(map, upper_interface, lower_interface, pname, config_path, name)
    {
        string protocol_type = std::get<string>(parameters.at(STRINGIFY(protocol_type)).value);
        is_LLC = (protocol_type.find("LLC") != string::npos);

        int processing_queue_size = std::get<int>(parameters.at(STRINGIFY(processing_queue_size)).value);
        string processing_q_arbiter_t = std::get<string>(parameters.at(STRINGIFY(processing_q_arbiter_t)).value);
        vector<int>* processing_q_arbiter_candidates_ids = NULL; //ids of the cores subjected to data access arbiteration
        
        if(processing_q_arbiter_t != STRINGIFY(NULL))
            processing_q_arbiter_candidates_ids = new vector<int>(std::get<vector<int>>(parameters.at(STRINGIFY(processing_q_arbiter_candidates_ids)).value));
        
        delete m_processing_queue;
        m_processing_queue = new ProcessingBuffer<Message>([&] (const Message& msg, EntryState state) -> EntryState {
                                                                return m_protocol->getRequestState(msg, state);},
                                                           processing_queue_size,
                                                           createArbiter(processing_q_arbiter_t, processing_q_arbiter_candidates_ids));

        if(processing_q_arbiter_t == STRINGIFY(RROFArbiter)) //processing queue arbiter
        {
            ((RROFArbiter*)m_processing_queue->getArbiter())->set_arbiter_name("cache_" + std::to_string(m_id), m_id);
            if(is_LLC)
                ((RROFArbiter*)m_processing_queue->getArbiter())->set_dir_arbiter();
            // else
            ((RROFArbiter*)m_processing_queue->getArbiter())->set_respect_order();
            
            is_RROF = true;
        }

        if(std::get<string>(parameters.at(STRINGIFY(arbiter_type)).value) == STRINGIFY(RROFArbiter)) //dataHandler arbiter
            ((RROFArbiter*)m_data_access_arbiter)->set_arbiter_name("cache_" + std::to_string(m_id) + "_Bank", m_id);

        RROFArbiter::cacheline_size = m_data_handler->getBlockSize();
        cause_repl_msg = Message();

        action_functions[ControllerAction::Type::CAUSED_REPLACEMENT] = [&](void* ptr) {causedReplacement(ptr);};
    }

    void PredictableDirectory::cycleProcess()
    {
        if(is_LLC)
            RROFArbiter::set_cycle(m_cache_cycle);

        CacheControllerDirectory::cycleProcess();
    }
    
    void PredictableDirectory::processLogic()
    {
        this->addRequests2ProcessingQueue(*m_processing_queue);

        Message ready_msg;
        if (m_processing_queue->getReady(&ready_msg) == false)
            return;

        if(ready_msg.source == Message::Source::LOWER_INTERCONNECT)
            Logger::getLogger()->updateRequest(ready_msg.msg_id, Logger::EntryId::CACHE_CHECKPOINT);
        
        // if(m_cache_cycle >= 765030)
        //     cout << "here\n";
        if((ready_msg.addr & 0xFFFFFFFFFFFFFFC0) == 140733183201344)//121773
            cout << "here\n";
        vector<ControllerAction> actions = m_protocol->processRequest(ready_msg, dprint);

        for (ControllerAction action : actions)
            action_functions[action.type](action.data);

        RROFArbiter::removeIfZeroCount(ready_msg);
        if(is_LLC && !is_RROF)
            RROFArbiter::addDependencyEntry(ready_msg);
    }


    void PredictableDirectory::addRequests2ProcessingQueue(ProcessingBuffer<Message> &buf)
    {
        Message msg;

        while (m_upper_interface->peekMessage(&msg))
        {
            if(find(msg.to.begin(), msg.to.end(), m_id) == msg.to.end()) //check if this message is destined to this controller
                m_upper_interface->popFrontMessage();
            else
            {
                msg.source = Message::Source::UPPER_INTERCONNECT;
                EntryState state = m_protocol->getRequestState(msg, EntryState::NonReady);
                if ((msg.data != NULL) && buf.pushFront(msg))
                    m_upper_interface->popFrontMessage();
                else if(buf.pushBack(msg, state))
                    m_upper_interface->popFrontMessage();
            }
        }

        while (m_lower_interface->peekMessage(&msg))
        {
            if(find(msg.to.begin(), msg.to.end(), m_id) == msg.to.end()) //check if this message is destined to this controller
                m_lower_interface->popFrontMessage();
            else
            { 
                // EntryState state;
                // if(is_LLC && is_RROF)
                //     state = EntryState::Ready;
                // else
                //     state = EntryState::NonReady;

                msg.source = Message::Source::LOWER_INTERCONNECT;
                if (buf.pushBack(msg, EntryState::NonReady))
                {   
                    if(is_LLC && is_RROF)
                        RROFArbiter::addDependencyEntry(msg); 
                    m_lower_interface->popFrontMessage();
                }
            }
        }

        this->checkReplacements(buf);
    }

    void PredictableDirectory::causedReplacement(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        cause_repl_msg = *msg;
        delete msg;
    }

    void PredictableDirectory::updateCacheLine(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        GenericCacheLine *cache_line = (GenericCacheLine *)((uint8_t *)data_ptr + sizeof(Message));
        
        // if(!is_LLC && msg->complementary_value == MOESIDirectory::REQUEST_TYPE_PUT_ACK)
        // {   //For replacements: one remove at L1 when line reaches I, and the other at LLC when the write finishes (only for dirty lines)
        //     Message ack_msg = *msg;
        //     ack_msg.owner = m_id;
        //     RROFArbiter::removeRequestFromQueues(ack_msg);
        // }
        // else if(!is_LLC && cache_line->valid == false && msg->source == Message::Source::SELF) //This happens when a line gets removed before the replacement action
        //     RROFArbiter::removeRequestFromQueues(*msg);
        // // else if(is_LLC && cache_line->valid == false && (msg->complementary_value == MOESIDirectory::REQUEST_TYPE_PUTM ||
        // //                                                  msg->complementary_value == MOESIDirectory::REQUEST_TYPE_PUTE || 
        // //                                                  msg->complementary_value == MOESIDirectory::REQUEST_TYPE_PUTO))
        // //     RROFArbiter::removeRequestByAddr(*msg);
        // else if(is_LLC && msg->complementary_value == MOESIDirectory::REQUEST_TYPE_PUTM && msg->data == NULL) //PUTM from nonOwner
        //     RROFArbiter::removeRequestFromQueues(*msg);
        // else
        // {
            GenericCacheLine old_line;
            m_data_handler->readLineBits(msg->addr, &old_line);

            if(!is_LLC && 
               (cache_line->state == old_line.state) && 
               (msg->complementary_value == MOESIDirectory::REQUEST_TYPE_INV || 
                msg->complementary_value == MOESIDirectory::REQUEST_TYPE_INVM))
            {//This happens when a line gets an invalidation while it's waiting for an ack to be replaced, so the invalidation will be ignored. 
             //Before removing (retiring) the invalidation request we check if it has a higher priority than the original replacement, we upgrade the replacement request to take the place invalidation.
                // Message copy_msg = *msg;
                // copy_msg.owner = m_id;

                // RROFArbiter::upgradeRequestByAddr(copy_msg);
                // RROFArbiter::removeRequestFromQueues(copy_msg);
                RROFArbiter::fixDependency(m_id, msg->addr, msg->msg_id);
            }
        // }
        
        if (msg->data == NULL || cache_line->valid == false)
            RROFArbiter::decrementEraseCount(*msg);

        CacheControllerDirectory::updateCacheLine(data_ptr);
    }

    void PredictableDirectory::writeCacheLineData(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        
        if(m_data_handler->isReady(msg->addr))
        {
            if(m_data_access_action.find(msg->msg_id) != m_data_access_action.end()) //delayed call of writeCacheLineData
                RROFArbiter::removeRequestFromQueues(*msg);
            else
                RROFArbiter::decrementEraseCount(*msg);
        }

        CacheControllerDirectory::writeCacheLineData(data_ptr);
    }

    void PredictableDirectory::performWriteBack(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        Message clone_msg = *msg;

        if (this->m_saved_requests_for_wb.find(this->getAddressKey(msg->addr)) !=
            this->m_saved_requests_for_wb.end())
        {   
            Message saved_msg = this->m_saved_requests_for_wb[this->getAddressKey(msg->addr)];
            if(saved_msg.owner != m_id && msg->to.size() != 0) //Happens in case of sending data to both (M->S)
                RROFArbiter::incrementEraseCount(saved_msg);
        }
        else if(msg->owner != m_id && msg->to.size() != 0) //Happens in case of sending data to both (M->S)
            RROFArbiter::incrementEraseCount(*msg);
        
        if(is_LLC && is_RROF && RROFArbiter::isOldestDemandWaiting(clone_msg))
        {
            if(msg->data == NULL)
            {
                GenericCacheLine cache_line;
                m_data_handler->readCacheLine(msg->addr, &cache_line);
                msg->copy(cache_line.m_data);
            }
            m_victim_cache[this->getAddressKey(msg->addr)] = *msg;
            delete msg;
            return;
        }
        else    
            RROFArbiter::incrementEraseCount(*msg);

        if(is_LLC && is_RROF)
        {   
            //Copied from Basecontroller
            if (clone_msg.owner == this->m_id)
                clone_msg.to.push_back(this->m_shared_memory_id);
            else
                clone_msg.to.push_back(clone_msg.owner); 
            RROFArbiter::updateDEntry(clone_msg);
        }
        CacheControllerDirectory::performWriteBack(data_ptr);
    }

    void PredictableDirectory::checkReplacements(ProcessingBuffer<Message> &buf)
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
            if (buf.pushBack(msg, EntryState::NonReady))
            {
                ((CacheDataHandler_COTS*)m_data_handler)->addressOfLinePendingWB(true, &evicted_address);
                RROFArbiter::addReplReq2Queues(msg, cause_repl_msg);
                if(is_LLC && is_RROF)
                    RROFArbiter::addDependencyEntry(msg); 

                if(!is_LLC)
                    Logger::getLogger()->addRequest(m_id, msg);
                else
                    Logger::getLogger()->addRequest(cause_repl_msg.owner, msg);

                cause_repl_msg = Message();
            }

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

    void PredictableDirectory::sendForwardMessage(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        for(int i = 0; i < msg->to.size(); i++)
            RROFArbiter::incrementEraseCount(*msg);
        // if(is_LLC)
        //     RROFArbiter::updateDEntry(*msg);

        CacheControllerDirectory::sendForwardMessage(data_ptr);
    }

    void PredictableDirectory::sendBusRequest(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        
        if(is_LLC && is_RROF && 
           m_victim_cache.find(getAddressKey(msg->addr)) != m_victim_cache.end())
        {
            msg->copy(m_victim_cache[getAddressKey(msg->addr)].data);
            msg->source = Message::Source::UPPER_INTERCONNECT;
            
            EntryState state = m_protocol->getRequestState(*msg, EntryState::NonReady);
            m_processing_queue->pushFront(*msg);
            m_victim_cache.erase(getAddressKey(msg->addr));
            
            RROFArbiter::incrementEraseCount(*msg);
            delete msg;
            return;
        }

        for(int i = 0; i < msg->to.size(); i++)
            RROFArbiter::incrementEraseCount(*msg);
        if(is_LLC && is_RROF)
            RROFArbiter::updateDEntry(*msg);
        // else
        // {
        //     if(msg->to[0] != m_shared_memory_id)
        //         RROFArbiter::updateDEntry(*msg);
        // }

        CacheControllerDirectory::sendBusRequest(data_ptr);
    }

    void PredictableDirectory::hitAction(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        
        if(m_data_access_action.find(msg->msg_id) == m_data_access_action.end()) //increament only with the first trial to hit
        {    
            RROFArbiter::incrementEraseCount(*msg);
            // if(is_LLC)
            //     RROFArbiter::updateDEntry(*msg);
        }

        CacheControllerDirectory::hitAction(data_ptr);
    }

    void PredictableDirectory::removePendingAndRespond(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        
        RROFArbiter::incrementEraseCount(*msg);
        // if(is_LLC)
        //     RROFArbiter::updateDEntry(*msg);

        CacheControllerDirectory::removePendingAndRespond(data_ptr);
    }
}