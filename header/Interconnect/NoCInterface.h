/*
 * File  :      NoCInterface.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Jan 1, 2024
 */

#ifndef _NOCINTERFACE_H
#define _NOCINTERFACE_H

#include "MeshInterface.h"

#include <map>

using namespace std;

namespace octopus
{
    class NoCInterface : public MeshInterface
    {
    protected:
        map<int, int> m_routing_table;

    public:
        NoCInterface(int id, int buffer_max_size, vector<int>& link_ids, vector<vector<int>>& connection_mat, vector<int>& switch_ids);

        virtual bool pushMessage(Message &msg, uint64_t cycle, MessageType type = MessageType::REQUEST) override; //Ignore type
        virtual bool noc_pushMessage(Message &msg);
    };
}

#endif /* _MESHINTERFACE_H */
