/*
 * File  :      CacheController_End2End.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 22, 2022
 */

#include "../header/CacheController_End2End.h"

#include "../header/RequestorsQueues.h"

namespace ns3
{
    // private controller constructor
    CacheController_End2End::CacheController_End2End(CacheXml &cacheXml, string &fsm_path, CommunicationInterface *upper_interface,
                                                     CommunicationInterface *lower_interface, bool cach2Cache,
                                                     vector<int> sharedMemId, CohProtType pType, int order, vector<int> *private_caches_id)
        : CacheController(cacheXml, fsm_path, upper_interface, lower_interface, cach2Cache, sharedMemId, pType, order, private_caches_id)
    {
        m_owner_of_latest_data = -1;
    }

    CacheController_End2End::~CacheController_End2End()
    {
    }

    void CacheController_End2End::addRequests2ProcessingQueue(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf)
    {
        Message msg;

        if (m_upper_interface->peekMessage(&msg))
        {
            if (m_shared_memory_id[0] == 100 && llc_nbnk)
            {
                AddrMapping::getAddrMapping()->addr_map(&msg);
            }
            if(msg.data != NULL)
                m_owner_of_latest_data = msg.owner;
        }

        CacheController::addRequests2ProcessingQueue(buf);
    }

    void CacheController_End2End::callActionFunction(ControllerAction action)
    {
        switch (action.type)
        {
            case ControllerAction::Type::SEND_INV_MSG: this->sendInvalidationMessage(action.data); return;

            default: CacheController::callActionFunction(action); return;
        }
    }

    void CacheController_End2End::sendBusRequest(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        Message *tmp_msg = NULL;
        Message returned_msg;

        // restore addr before sending out from shared cache
        if (m_shared_memory_id[0] == 100 && llc_nbnk )
        {
            // create new message before send 2 RX.
            // This avoid double addr_map_restore is the logic goes to CacheController::sendBusRequest and do addr_map_restore again.
            tmp_msg = msg;
            msg = new Message(*tmp_msg);
            AddrMapping::getAddrMapping()->addr_map_restore(msg, this->m_core_order);
        }

        if(m_upper_interface->rollback(msg->addr, this->m_cache_line_size, &returned_msg))
        {
            if(returned_msg.data != NULL)
            {
                msg->copy(returned_msg.data);
                m_upper_interface->pushMessage2RX(*msg, MessageType::DATA_RESPONSE);

                if (globalQueues_en)
                {
                    if (m_shared_memory_id[0] == 100)
                    {
                        RequestorsQueues::getReqQObj()->getRequestorsQueues()->removeRequest(returned_msg.owner, returned_msg.msg_id);
                        if(log_enable)
                            cout << "sendBusRequestE2E: remove: " << returned_msg.msg_id << " core: " << returned_msg.owner << " size: " << RequestorsQueues::getReqQObj()->getRequestorsQueues()->getRequestorSize(returned_msg.owner) << " id " << this->m_core_id<< endl;
                    }
                }
            }
            else
            {
                cout << "CacheController_End2End(id = " << this->m_core_id << "): Wrong message returned from the rollback" << endl;
                exit(0);
            }
            delete msg;
            if (tmp_msg){
                delete tmp_msg;
            }
        }
        else
        {
            CacheController::sendBusRequest(data_ptr);
        }
    }

    void CacheController_End2End::performWriteBack(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;

        if(msg->data == NULL)
        {
            if(!checkReadinessOfCache(*msg, ControllerAction::Type::WRITE_BACK, data_ptr))
                return;
            GenericCacheLine cache_line;
            m_data_handler->readCacheLine(msg->addr, &cache_line);
            msg->copy(cache_line.m_data);
        }
        
        //msg->owner = (m_owner_of_latest_data > -1) ? m_owner_of_latest_data : this->m_core_id;

	if(m_wb_cores.find (msg->msg_id) != m_wb_cores.end() )
        {
            msg->owner = m_wb_cores [msg->msg_id] ;
            m_wb_cores.erase (msg->msg_id);
            m_wb_address.erase(msg->addr);
        }
        else
        { // this happens when a created WB in LLC is replaced by a PutM from L1

            if(m_wb_address.find (msg->addr) != m_wb_address.end())
            {
                if(m_wb_cores.find (m_wb_address[msg->addr]) != m_wb_cores.end())
                {
                    if (globalQueues_en)
                    {
                        RequestorsQueues::getReqQObj()->getRequestorsQueues()->removeRequest(m_wb_cores[m_wb_address[msg->addr]], m_wb_address[msg->addr]);
                        if(log_enable)
                            cout << "SA: performWriteBack: remove: " << m_wb_address[msg->addr] << " core: " << m_wb_cores[m_wb_address[msg->addr]]  << " address " << msg->addr << endl;
                    }
                m_wb_cores.erase (m_wb_address[msg->addr]);
                m_wb_address.erase(msg->addr);
                }
            }
        }

        msg->to.push_back(this->m_shared_memory_id[0]); //address map

        // restore addr before sending out from shared cache
        if (m_shared_memory_id[0] == 100 && llc_nbnk )
        {
            AddrMapping::getAddrMapping()->addr_map_restore(msg, this->m_core_order);
        }

        if (!m_upper_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::DATA_RESPONSE))
        {
            cout << "CacheController: Cannot insert the Msg into BusTxResp FIFO, FIFO is Full" << endl;
            exit(0);
        }

        delete msg;
    }
    
    void CacheController_End2End::sendInvalidationMessage(void *data_ptr)
    {
        Message *msg = (Message *)data_ptr;
        msg->cycle = this->m_cache_cycle;

        
        // restore addr before sending out from shared cache
        if (m_shared_memory_id[0] == 100 && llc_nbnk )
        {
            AddrMapping::getAddrMapping()->addr_map_restore(msg,this->m_core_order);
        }
        if (!m_lower_interface->pushMessage(*msg, this->m_cache_cycle, MessageType::SERVICE_REQUEST))
        {
            cout << "CacheController_End2End(id = " << this->m_core_id << "): Cannot insert the Msg into the lower interface FIFO, FIFO is Full" << endl;
            exit(0);
        }

        delete msg;
    }

    void CacheController_End2End::initialize(uint64_t address, const uint8_t* data, int size) //for Initializable
    {
        // GenericCacheLine cache_line;
        // if (this->m_data_handler->readLineBits(address, &cache_line) && this->m_cache_cycle > 1)
        // {
        //     this->m_data_handler->modifyData(address, data, size, true);
            
        //     // send Bus request, update cache line
        //     Message *msg = new Message(IdGenerator::nextReqId(),                        // Id
        //                                address,                                         // Addr
        //                                0,                                               // Cycle
        //                                (uint16_t)MSIProtocol::REQUEST_TYPE_SILENT_INV,  // Complementary_value
        //                                (uint16_t)this->m_core_id);                      // Owner
        //     msg->to.push_back((uint16_t)this->m_core_id);
        //     msg->source = Message::Source::UPPER_INTERCONNECT;
        //     this->sendInvalidationMessage((void*)msg);
        // }
        // else
            CacheController::initialize(address, data, size);
    }
void CacheController_End2End::checkReplacements(FRFCFS_Buffer<Message, CoherenceProtocolHandler> &buf)
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
            msg.to.push_back((uint16_t)this->m_shared_memory_id[0]);
            if (buf.pushBack(msg, FRFCFS_State::NonReady))
                ((CacheDataHandler_COTS*)m_data_handler)->addressOfLinePendingWB(true, &evicted_address);
            
            if (globalQueues_en)
            {
                if(m_shared_memory_id[0] == 100)
                {
                RequestorsQueues::getReqQObj()->getRequestorsQueues()->addRequest(m_owner_of_latest_data,msg.msg_id,1, msg.addr);
                //RequestorsQueues::getReqQObj()->getRequestorsQueues()->addRequest(m_owner_of_latest_data,msg.msg_id,1);
                m_wb_cores [msg.msg_id] = m_owner_of_latest_data;
                if(log_enable)
                    cout << "checkReplacementsE2E: add: " << msg.msg_id << " core: " <<  m_owner_of_latest_data<< " size: " << RequestorsQueues::getReqQObj()->getRequestorsQueues()->getRequestorSize(m_owner_of_latest_data)<< " address: "<< evicted_address << endl;

                }
            }
            

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
}
