/*
 * File  :      DirectoryProtocol.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 10, 2023
 */

#ifndef _DirectoryProtocol_H
#define _DirectoryProtocol_H

#include "CoherenceProtocolHandler.h"
#include <list>

namespace octopus
{
    class DirectoryProtocol : public CoherenceProtocolHandler
    {
    protected:
        // key is the addr & mask(nbits of CacheLineSize) and the value is a list of sharers ids
        std::map<uint64_t, std::list<uint64_t>>* protocol_counters;
        // key is the addr & mask(nbits of CacheLineSize) and the value is ack counter
        std::map<uint64_t, int64_t>* ack_counters;

    public:
        DirectoryProtocol(CacheDataHandler *cache, const std::string &fsm_path, int id, int sharedMemId) : 
            CoherenceProtocolHandler(cache, fsm_path, id, sharedMemId) {}
        ~DirectoryProtocol() {};

        virtual std::vector<ControllerAction> processRequest(Message &request_msg, DebugPrint* dprint = NULL) {return std::vector<ControllerAction>();}
        virtual FRFCFS_State getRequestState(const Message &, FRFCFS_State) {return FRFCFS_State::Ready;}

        virtual void setProtocolCounters(std::map<uint64_t, std::list<uint64_t>>* counter_ptr) {this->protocol_counters = counter_ptr;}
        virtual void setACKCounters(std::map<uint64_t, int64_t>* counter_ptr) {this->ack_counters = counter_ptr;}
    };
}

#endif
