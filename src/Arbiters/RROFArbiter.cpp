/*
 * File  :      RROFArbiter.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 21, 2022
 */

#include "../../header/Arbiters/RROFArbiter.h"

namespace ns3
{
    vector<vector<RROFArbiter::QEntry>> *RROFArbiter::global_queues = NULL;
    map<uint64_t, vector<RROFArbiter::DEntry>> *RROFArbiter::dependency_map = NULL; 
    vector<int>* RROFArbiter::v_size = NULL;
    int RROFArbiter::cacheline_size = 64;
    uint64_t RROFArbiter::m_cycle = 0;
    int RROFArbiter::size_limit = 0;
    bool RROFArbiter::m_full_arbiter = true;

    RROFArbiter::RROFArbiter(vector<int> *candidates_ids, int arbiter_period) : RRArbiter(candidates_ids, arbiter_period)
    {
        if(!global_queues)
        {    
            global_queues = new vector<vector<RROFArbiter::QEntry>>();
            dependency_map = new map<uint64_t, vector<DEntry>>;
            v_size = new vector<int>();
        }

        while(global_queues->size() < candidates_ids->size())
        {
            global_queues->push_back(vector<QEntry>());
            v_size->push_back(0); //initialize by zero
        }
    }

    RROFArbiter::~RROFArbiter()
    {
        if(global_queues)
        {
            delete global_queues;
            delete dependency_map;
        }
    }

    void RROFArbiter::set_arbiter_name(string name, int id)
    {
        this->stage_name = name;
        this->stage_id = id;
    }
    
    void RROFArbiter::set_dir_arbiter()
    {
        this->is_dir_arbiter = true;
    }

    void RROFArbiter::set_respect_order()
    {
        this->respect_dir_order = true;
    }
        
    void RROFArbiter::set_cycle(uint64_t cycle)
    {
        m_cycle = cycle;
    }

    int RROFArbiter::findMessage(vector<Message>& buffer, int id) //Find the oldest
    {
        int oldest_index = -1;
        if(buffer.size() == 0)
            return oldest_index;

        for(int i = 0; i < global_queues->at(id).size(); i++)
        {
            auto iter = find(buffer.begin(), buffer.end(), global_queues->at(id)[i].msg_id);
            if(i == 0 && iter == buffer.end()) //pick the top dependee in the chain
            {
                int top_msg_id;
                int top_q_id;
                if((top_msg_id = getChainTop(global_queues->at(id)[i].msg_id, global_queues->at(id)[i].addr, &top_q_id)) != -1)
                {    
                    if((iter = find(buffer.begin(), buffer.end(), top_msg_id)) != buffer.end())
                    {
                        vector<QEntry>::iterator entry_itr;
                        findEntry(top_msg_id, top_q_id, &entry_itr);
                       
                        oldest_index = iter - buffer.begin();
                        if(is_dir_arbiter)
                        {
                            entry_itr->dir_cycle = m_cycle;
                            addDependencyEntry(top_q_id, iter->msg_id, entry_itr->addr);
                        }
                        entry_itr->stages.push_back(make_pair(stage_name, m_cycle));
                        removeDestination(top_q_id, top_msg_id, entry_itr->addr, stage_id);
                        break;
                    }
                }
            }

            if(iter != buffer.end() && !(respect_dir_order && isDepender(id, iter->msg_id, global_queues->at(id)[i].addr, stage_id)))
            {
                oldest_index = iter - buffer.begin();
                if(is_dir_arbiter)
                {
                    global_queues->at(id)[i].dir_cycle = m_cycle;
                    addDependencyEntry(id, iter->msg_id, global_queues->at(id)[i].addr);
                }
                global_queues->at(id)[i].stages.push_back(make_pair(stage_name, m_cycle));
                removeDestination(id, iter->msg_id, global_queues->at(id)[i].addr, stage_id);
                break;
            }
        }

        return oldest_index;
    }
    
    void RROFArbiter::addDependencyEntry(int id, int msg_id, uint64_t addr)
    {
        if(dependency_map->find(addr) != dependency_map->end())
        {
            auto d_iter = find_if((*dependency_map)[addr].begin(), (*dependency_map)[addr].end(),
            [=](auto p) -> bool {
                return (p.queue_id == id) && (p.msg_id == msg_id);
            });
            if(d_iter == (*dependency_map)[addr].end())
                (*dependency_map)[addr].push_back(DEntry(id, msg_id, 64));
        }
        else
            (*dependency_map)[addr].push_back(DEntry(id, msg_id, 64));
    }

    void RROFArbiter::addDependencyEntry(Message& msg)
    {
        // if(m_full_arbiter)
        // {
        //     cout << "Error: This method is for non full arbiters only" << endl;
        //     exit(0);
        // }
        
        vector<QEntry>::iterator itr;
        int q_id;
        if(!findEntry(msg.msg_id, &itr, &q_id))
            return;
        
        addDependencyEntry(q_id, itr->msg_id, itr->addr);
    }

    bool RROFArbiter::isDepender(Message& msg)
    {
        if(m_full_arbiter)
        {
            cout << "Error: This method is for non full arbiters only" << endl;
            exit(0);
        }
        
        vector<QEntry>::iterator itr;
        int q_id;
        if(!findEntry(msg.msg_id, &itr, &q_id))
        {
            cout << "Error: Couldn't find the entry" << endl;
            exit(0);
        }
        return isDepender(q_id, itr->msg_id, itr->addr, -1);
    }

    void RROFArbiter::removeDependencyEntry(int id, int msg_id, uint64_t addr)
    {
        auto m_iter = dependency_map->find(addr);
        if(m_iter != dependency_map->end())
        {
            auto d_iter = find_if((*dependency_map)[addr].begin(), (*dependency_map)[addr].end(),
            [=](auto p) -> bool {
                return (p.queue_id == id) && (p.msg_id == msg_id);
            });
            if(d_iter != (*dependency_map)[addr].end())
            {    
                (*dependency_map)[addr].erase(d_iter);
                if((*dependency_map)[addr].empty())
                    dependency_map->erase(m_iter);
            }
        }
    }

    bool RROFArbiter::isDepender(int id, int msg_id, uint64_t addr, int stage_id)
    {
        auto m_iter = dependency_map->find(addr);
        if(m_iter != dependency_map->end())
        {
            auto d_iter = find_if((*dependency_map)[addr].begin(), (*dependency_map)[addr].end(),
            [=](auto p) -> bool {
                return (p.queue_id == id) && (p.msg_id == msg_id);
            });
            if(d_iter == (*dependency_map)[addr].end()) //not Found
                return false;
            else if(d_iter == (*dependency_map)[addr].begin()) //Top of the dependency chain
                return false;
            // else if (stage_name == "sBus")
            // {
            //     for(auto d : m_iter->second)
            //     {
            //         vector<QEntry>::iterator entry_itr;
                    
            //         if((d.first == id) && (d.second == msg_id))
            //             return false;
                    
            //         findEntry(d.second, d.first, &entry_itr);
            //         auto stage_iter = find_if(entry_itr->stages.begin(), entry_itr->stages.end(),
            //         [=](auto p) -> bool {
            //             if(p.first == entry_itr->stages.begin()->first && 
            //                p.second == entry_itr->stages.begin()->second) //skip first stage
            //                return false;
            //             else
            //                 return (p.first == stage_name);
            //         });
            //         if(stage_iter == entry_itr->stages.end())//an entry that is placed higher in the dependency chain hasn't reached this stage yet
            //             return true;
            //     }
            // }
            else if(stage_id == 100 || stage_id == 64)
            {
                vector<DEntry> sub_dlist;
                for(auto d_entry: m_iter->second)
                {
                    auto dest_itr = find(d_entry.destinations.begin(), d_entry.destinations.end(), stage_id);
                    if(dest_itr != d_entry.destinations.end())
                        sub_dlist.push_back(d_entry);
                }

                if(sub_dlist.empty())
                    return false;
                else if(*d_iter == sub_dlist[0])//d_iter is the top of the sub dependency chain
                    return false;
                else
                    return true;   
            }
            else 
                return true;
        }
        return false;
    }
    
    int RROFArbiter::getChainTop(int msg_id, uint64_t addr, int* q_id)
    {
        if(dependency_map->find(addr) != dependency_map->end())
        {
            int top_msg_id = dependency_map->at(addr).at(0).msg_id;

            if(top_msg_id == msg_id)
                return -1;
            else
            {   
                *q_id = dependency_map->at(addr).at(0).queue_id;
                return top_msg_id;
            }
        }
        return -1;
    }

    bool RROFArbiter::findEntry(int id, vector<QEntry>::iterator *itr, int* q_id)
    {
        for(int i = 0; i < global_queues->size(); i++)
        {
            *itr = find(global_queues->at(i).begin(), global_queues->at(i).end(), id);
            if(*itr != global_queues->at(i).end())
            {
                if(q_id != NULL)
                    *q_id = i;
                return true;
            }
        }
        return false;
    }

    bool RROFArbiter::findEntry(int id, int q_index, vector<QEntry>::iterator *itr)
    {
        *itr = find(global_queues->at(q_index).begin(), global_queues->at(q_index).end(), id);
        
        return (*itr != global_queues->at(q_index).end());
    }

    void RROFArbiter::addDemandReq2Queues(Message& msg)
    {
        if(!global_queues)
            return;
        QEntry entry = QEntry(msg);
        
        if(!m_full_arbiter)
            entry.stages.push_back(make_pair("Add", m_cycle));
        
        global_queues->at(msg.owner).push_back(entry);
        (*v_size)[msg.owner] += 3; //Add 3 to count for the possible replacement requests that can result from this demand request
            
    }

    void RROFArbiter::addReplReq2Queues(Message& msg, Message& demand_msg)
    {
        if(!global_queues)
            return;

        vector<QEntry>::iterator iter;
        if(findEntry(demand_msg.msg_id, demand_msg.owner, &iter))
        {
            QEntry entry = QEntry(msg, QStatus::NO_STATUS, QType::REPLACEMENT);
            entry.created_by = demand_msg.msg_id;

            if(!m_full_arbiter)
                entry.stages.push_back(make_pair("Add", m_cycle));

            global_queues->at(demand_msg.owner).insert(iter + 1, entry);
        }
        else
        {
            cout << "Error: Couldn't find the demand request" << endl;
            exit(0);
        }
    }

    void RROFArbiter::removeRequestFromQueues(Message& msg)
    {
        if(!global_queues)
            return;

        for(int i = 0; i < global_queues->size(); i++)
        {
            vector<QEntry>::iterator iter;
            if(findEntry(msg.msg_id, i, &iter))
            {
                iter->erase_count--;
                if(iter->erase_count == 0)
                {    
                    removeDependencyEntry(i, iter->msg_id, iter->addr);

                    if(!m_full_arbiter)
                    {
                        iter->stages.push_back(make_pair("retire", m_cycle));
                        ((LoggerPredictable*)Logger::getLogger())->retireRequest(iter->msg_id, iter->stages, 
                                                                                 iter->type == QType::DEMAND,       //is Demand?
                                                                                 true); //is the oldest?

                    }
                    else
                    {
                        ((LoggerPredictable*)Logger::getLogger())->retireRequest(iter->msg_id, iter->stages, 
                                                                                 iter->type == QType::DEMAND,       //is Demand?
                                                                                 iter == global_queues->at(i).begin()); //is the oldest?
                    }
                    decrementVirtualSize(i, iter);
                    global_queues->at(i).erase(iter);
                }
                return;
            }
        }
        
        cout << "Error: Couldn't find the request" << endl;
        exit(0);
    }

    void RROFArbiter::incrementEraseCount(Message& msg)
    {
        if(!global_queues)
            return;

        vector<QEntry>::iterator iter;
        if(findEntry(msg.msg_id, &iter))
            iter->erase_count++;
        else
        {
            cout << "Error: Couldn't find the request" << endl;
            exit(0);
        }
    }

    void RROFArbiter::decrementEraseCount(Message& msg)
    {
        if(!global_queues)
            return;

        vector<QEntry>::iterator iter;
        if(findEntry(msg.msg_id, &iter))
            iter->erase_count--;
        else
        {
            cout << "Error: Couldn't find the request" << endl;
            exit(0);
        }
    }

    void RROFArbiter::removeIfZeroCount(Message& msg)
    {
        if(!global_queues)
            return;

        for(int i = 0; i < global_queues->size(); i++)
        {
            vector<QEntry>::iterator iter;
            if(findEntry(msg.msg_id, i, &iter))
            {
                if(iter->erase_count == 0)
                {    
                    removeDependencyEntry(i, iter->msg_id, iter->addr);

                    if(!m_full_arbiter)
                    {    
                        iter->stages.push_back(make_pair("retire", m_cycle));
                        ((LoggerPredictable*)Logger::getLogger())->retireRequest(iter->msg_id, iter->stages, 
                                                                                iter->type == QType::DEMAND,       //is Demand?
                                                                                true); //is the oldest?                        
                    }
                    else
                    {
                        ((LoggerPredictable*)Logger::getLogger())->retireRequest(iter->msg_id, iter->stages, 
                                                                                iter->type == QType::DEMAND,       //is Demand?
                                                                                iter == global_queues->at(i).begin()); //is the oldest?
                    }
                    decrementVirtualSize(i, iter);
                    global_queues->at(i).erase(iter);
                }
                return;
            }
        }
    }

    void RROFArbiter::removeRequestByAddr(Message& msg)
    {
        // for(int i = 0; i < global_queues->size(); i++)
        // {
        //     auto itr = find_if(global_queues->at(i).begin(), global_queues->at(i).end(), [&](QEntry entry) -> bool {
        //         return entry.addr == (msg.addr & ~uint64_t(cacheline_size-1)) && entry.msg_id != msg.msg_id;
        //     });
            
        //     if(itr != global_queues->at(i).end())
        //     {
        //         removeDependencyEntry(i, itr->msg_id, itr->addr);
        //         global_queues->at(i).erase(itr);
        //         return;
        //     }
        // }

        cout << "Error: Couldn't find the request" << endl;
        exit(0);
    }

    void RROFArbiter::decrementReplacementEraseCount(int q_id, uint64_t addr)
    {
        // auto itr = find_if(global_queues->at(q_id).begin(), global_queues->at(q_id).end(), [&](QEntry entry) -> bool {
        //     return entry.addr == (addr & ~uint64_t(cacheline_size-1)) && entry.type == QType::REPLACEMENT;
        // });
        
        // if(itr != global_queues->at(q_id).end())
        // {
        //     if(itr->erase_count > 1)
        //         itr->erase_count--;
        // }
    }
    
    void RROFArbiter::upgradeRequestByAddr(Message& msg)
    {
        vector<QEntry>::iterator iter;
        int q_index = msg.owner;
        if(findEntry(msg.msg_id, q_index, &iter))
        {
            auto upgrade_iter = find_if(global_queues->at(q_index).begin(), global_queues->at(q_index).end(), 
            [&](QEntry entry) -> bool {
                return entry.addr == iter->addr && entry.msg_id != iter->msg_id && entry.type == iter->type;
            });

            if(upgrade_iter != global_queues->at(q_index).end())
            {
                if(upgrade_iter->erase_count == 1) //Increase the earse count if it is 1. The upgrade function is used only when two replacements in L1 and LLC happen concurrently.
                    upgrade_iter->erase_count = 2; //PutM and PutO comes with 2 earse counts, while PutE requires only 1 earse count. However, in the case of the upgrade, PutE will need 2 earses: 
                    //1) one at L1 after receiving Put_Ack and 2) the second at the DRAM after receiving data due to the replacement of the LLC.

                int upgrade_index = upgrade_iter - global_queues->at(q_index).begin();
                int original_index = iter - global_queues->at(q_index).begin();
                if(original_index < upgrade_index)
                {
                    QEntry entry = *upgrade_iter;

                    global_queues->at(q_index).erase(upgrade_iter);
                    global_queues->at(q_index).insert(iter, entry);
                }
            }
            else
            {
                cout << "Error: Couldn't find the request" << endl;
                exit(0);
            }
        }
        // else
        // {
        //     cout << "Error: Couldn't find the request" << endl;
        //     // exit(0);
        // }
    }

    vector<vector<RROFArbiter::QEntry>> const* RROFArbiter::getQueues()
    {
        return global_queues;
    }

    // int RROFArbiter::getVirtualSize(int q_id) // one implemenation
    // {
    //     int v_size = 0;
    //     int true_size = global_queues->at(q_id).size();

    //     for(int i = 0; i < true_size; i++)
    //     {
    //         if(global_queues->at(q_id)[i].type == QType::REPLACEMENT)
    //             v_size++;
    //         else if(global_queues->at(q_id)[i].type == QType::DEMAND)
    //         {
    //             v_size += 3; //Add 3 to count for the possible replacement requests that can result from this demand request
    //             if(((i+1) < true_size) && global_queues->at(q_id)[i+1].type == QType::REPLACEMENT) //Should be a replacement created by this demand request
    //                 v_size--;
    //             if(((i+2) < true_size) && global_queues->at(q_id)[i+2].type == QType::REPLACEMENT) //Should be a replacement created by this demand request
    //                 v_size--;
    //         }
    //     }

    //     return v_size;
    // }

    int RROFArbiter::getVirtualSize(int q_id) //another implemenation
    {
        return v_size->at(q_id);
    }

    void RROFArbiter::decrementVirtualSize(int q_id, vector<QEntry>::iterator itr)
    {
        int index = itr - global_queues->at(q_id).begin();
        int true_size = global_queues->at(q_id).size();

        if(itr->type == QType::REPLACEMENT)
        {
            if(((index-1) >= 0) && global_queues->at(q_id)[index-1].msg_id == itr->created_by) //Check if the demand request still present in the queues
                return;
            if(((index-2) >= 0) && global_queues->at(q_id)[index-2].msg_id == itr->created_by) //Check if the demand request still present in the queues
                return;

            (*v_size)[q_id]--;
        }
        else if(itr->type == QType::DEMAND)
        {
            (*v_size)[q_id]--;
            
            if(((index+1) < true_size) && global_queues->at(q_id)[index+1].created_by != itr->msg_id) //Should be a replacement created by this demand request
                (*v_size)[q_id]--;
            else if((index+1) >= true_size)
                (*v_size)[q_id]--;
            
            if(((index+2) < true_size) && global_queues->at(q_id)[index+2].created_by != itr->msg_id) //Should be a replacement created by this demand request
                (*v_size)[q_id]--;
            else if((index+2) >= true_size)
                (*v_size)[q_id]--;
        }
    }

    bool RROFArbiter::spaceForRequest(int q_id)
    {
        if(size_limit <= 0)
            return true;
        else
            return (getVirtualSize(q_id) + 3) <= size_limit;
    }

    void RROFArbiter::setSizeLimit(int limit)
    {
        size_limit = limit;
    }

    void RROFArbiter::disableArbiter()
    {
        m_full_arbiter = false;
    }

    void RROFArbiter::updateDEntry(Message& msg)
    {
        uint64_t addr = msg.addr & ~uint64_t(cacheline_size - 1);
        auto m_iter = dependency_map->find(addr);
        if(m_iter != dependency_map->end())
        {
            auto d_iter = find((*dependency_map)[addr].begin(), (*dependency_map)[addr].end(),msg.msg_id);
            if(d_iter != (*dependency_map)[addr].end())
            {
                for(int dest_id: msg.to)
                    d_iter->destinations.push_back(dest_id);
            }
            else
            {
                cout << "Error: Couldn't find DEntry" << endl;
                exit(0);
            }
        }
        else
        {
            cout << "Error: Couldn't find DEntry" << endl;
            exit(0);
        }
    }

    void RROFArbiter::removeDestination(int id, int msg_id, uint64_t addr, int stage_id)
    {
        auto m_iter = dependency_map->find(addr);
        if(m_iter != dependency_map->end())
        {
            auto d_iter = find_if(m_iter->second.begin(), m_iter->second.end(),
            [=](auto p) -> bool {
                return (p.queue_id == id) && (p.msg_id == msg_id);
            });
            if(d_iter == m_iter->second.end()) //not Found
                return;
            
            auto dest_itr = find(d_iter->destinations.begin(), d_iter->destinations.end(), stage_id);
            if(dest_itr != d_iter->destinations.end())
                d_iter->destinations.erase(dest_itr);
        }
    }

    void RROFArbiter::fixDependency(int q_id, uint64_t addr, uint64_t old_msg_id)
    {
        auto itr = find_if(global_queues->at(q_id).begin(), global_queues->at(q_id).end(), 
        [=](auto e) -> bool {
            return (e.addr == addr) && (e.type == QType::REPLACEMENT) && (e.msg_id != old_msg_id);
        });

        if(itr == global_queues->at(q_id).end())
        {
            cout << "Error in fixDependency function\n";
            exit(0);
        }

        auto m_iter = dependency_map->find(addr);
        if(m_iter != dependency_map->end())
        {
            auto d_iter_old = find_if(m_iter->second.begin(), m_iter->second.end(),
            [=](auto p) -> bool {
                return p.msg_id == old_msg_id;
            });

            auto d_iter_new = find_if(m_iter->second.begin(), m_iter->second.end(),
            [=](auto p) -> bool {
                return p.msg_id == itr->msg_id;
            });
            
            if(d_iter_old == m_iter->second.end()) //not Found
            {
                cout << "Error in fixDependency function\n";
                exit(0);
            }

            if(d_iter_new == m_iter->second.end())
                m_iter->second.insert(d_iter_old, DEntry(q_id, itr->msg_id, 64));
            else if (d_iter_old < d_iter_new)
            {
                DEntry tmp_entry = *d_iter_new;
                m_iter->second.erase(d_iter_new);
                m_iter->second.insert(d_iter_old, tmp_entry);
            }
        }
    }

    bool RROFArbiter::isOldestDemandWaiting(Message& msg)
    {
        vector<QEntry>::iterator itr; 
        int q_id;
        findEntry(msg.msg_id, &itr, &q_id);

        auto m_iter = dependency_map->find(itr->addr);

        if(m_iter != dependency_map->end())
        {
            bool pos_found = false;
            for(auto d_entry: m_iter->second)
            {
                if(d_entry.msg_id == msg.msg_id)
                    pos_found = true;
                else if(pos_found)
                {
                    vector<QEntry>::iterator depending_itr;
                    findEntry(d_entry.msg_id, d_entry.queue_id, &depending_itr);
                    if(depending_itr == global_queues->at(d_entry.queue_id).begin())
                        return true;
                }
            }
        }

        return false;
    }
}