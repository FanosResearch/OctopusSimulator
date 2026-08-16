/*
 * File  :      RROFArbiter.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 21, 2022
 */

#ifndef _RROF_ARBITER_H
#define _RROF_ARBITER_H

#include "RRArbiter.h"
#include "../LoggerPredictable.h"
#include <utility>

using namespace std;

namespace ns3
{
    class RROFArbiter: public RRArbiter
    {
    public:
        enum QStatus
        {
            NO_STATUS,
            STALL_L1,
            STALL_LLC
        };
        enum QType
        {
            DEMAND,
            REPLACEMENT,
        };
        struct QEntry
        {
            int msg_id = -1;
            QStatus status = QStatus::NO_STATUS;
            QType type = QType::DEMAND;
            int erase_count = 1;
            uint64_t addr = 0;                      //The address is masked by the cache line size.
            uint64_t dir_cycle = 0;                 //The cycle at which the entry is served by the Dir.
            int created_by = -1;                    //Msg Id of the demand request that was the result of this repl. request.

            vector<pair<string, uint64_t>> stages;  //First is the stage name and second is the serving cycle.


            QEntry(Message& msg, QStatus status = QStatus::NO_STATUS, QType type = QType::DEMAND)
            {
                this->msg_id = msg.msg_id;
                this->addr = msg.addr & ~uint64_t(cacheline_size - 1);
                this->status = status;
                this->type = type;
            }

            bool operator==(const QEntry& entry)
            {
                return this->msg_id == entry.msg_id;
            }

            bool operator==(const int msg_id)
            {
                return this->msg_id == msg_id;
            }
        };
        struct DEntry
        {
            int queue_id;
            int msg_id;
            vector<int> destinations;
            
            DEntry(int queue_id, int msg_id, int destination = -1)
            {
                this->msg_id = msg_id;
                this->queue_id = queue_id;
                
                if(destination != -1)
                    destinations.push_back(destination);
            }
            
            bool operator==(const DEntry& entry)
            {
                return (this->queue_id == entry.queue_id) && (this->msg_id == entry.msg_id);
            }

            bool operator==(const int msg_id)
            {
                return this->msg_id == msg_id;
            }
        };

    private:    
        bool is_dir_arbiter = false;
        bool respect_dir_order = false;
        string stage_name = "";
        int stage_id = -1;
        
        static uint64_t m_cycle;
        static bool m_full_arbiter;

        static vector<vector<QEntry>> *global_queues;
        static vector<int> *v_size;
        static int size_limit;
        static map<uint64_t, vector<DEntry>> *dependency_map;       //Key is the address, the first value is the queueId (coreId), and the second value is the msg_id.
        static bool findEntry(int id, vector<QEntry>::iterator *itr, int* q_id=NULL);
        static bool findEntry(int id, int q_index, vector<QEntry>::iterator *itr);
        static void addDependencyEntry(int id, int msg_id, uint64_t addr);
        static void removeDependencyEntry(int id, int msg_id, uint64_t addr);
        static bool isDepender(int id, int msg_id, uint64_t addr, int stage_id);
        static int getChainTop(int msg_id, uint64_t addr, int* q_id);
        static void decrementVirtualSize(int q_id, vector<QEntry>::iterator); //should be called before removing the entry from global queues
        static void removeDestination(int id, int msg_id, uint64_t addr, int stage_id);

    protected:
        virtual int findMessage(vector<Message> &buffer, int id) override;
        
    public:
        RROFArbiter(vector<int>* candidates_ids, int arbiter_period);
        ~RROFArbiter();

        void set_arbiter_name(string, int);
        void set_dir_arbiter();
        void set_respect_order();

        static int cacheline_size;

        static void set_cycle(uint64_t);
        static void addDemandReq2Queues(Message& msg);
        static void addReplReq2Queues(Message& msg, Message& demand_msg);
        static void removeRequestFromQueues(Message& msg);
        static void incrementEraseCount(Message& msg);
        static void decrementEraseCount(Message& msg);
        static void removeIfZeroCount(Message& msg);
        static void removeRequestByAddr(Message& msg); //find entry that maches the same address of msg
        static void decrementReplacementEraseCount(int q_id, uint64_t addr); //find replacement entry that maches the same address of msg
        static void upgradeRequestByAddr(Message& msg); //find entry that maches the same address of msg and upgraded if possible
        static vector<vector<QEntry>> const* getQueues();
        static int getVirtualSize(int q_id);
        static bool spaceForRequest(int q_id);
        static void setSizeLimit(int limit);
        static void updateDEntry(Message& msg);
        static void disableArbiter();
        static void addDependencyEntry(Message& msg); //Used only by non Full arbiters
        static bool isDepender(Message& msg);  //Used only by non Full arbiters
        static void fixDependency(int q_id, uint64_t addr, uint64_t old_msg_id);  //one request take the place of another in the chain
        static bool isOldestDemandWaiting(Message& msg);
    };
}

#endif /* _RROF_ARBITER_H */
