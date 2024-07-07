/*
 * File  :      TripleBus.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 16, 2022
 */

#include "../../header/Interconnect/TripleBus.h"

namespace octopus 
{
    TripleBus::TripleBus(ParametersMap map, string pname, string config_path, string name) : Bus(map, NULL, pname, config_path, name)
    {
        //Parameters initialization
        vector<int> upper_level_cache_ids = std::get<vector<int>>(parameters.at(STRINGIFY(upper_level_cache_ids)).value);
        int buffers_max_size = std::get<int>(parameters.at(STRINGIFY(buffers_max_size)).value);
       
       //Constructor
       m_interfaces.clear();
       m_topology.clear();

        for (auto id : upper_level_cache_ids)
            m_interfaces.push_back(new TripleBusInterface(id, buffers_max_size));

        for (auto id : m_lower_level_ids)
            m_interfaces.push_back(new TripleBusInterface(id, buffers_max_size));

        for (int i = 0; i < (int)m_interfaces.size(); i++)
        {
            m_topology[m_interfaces[i]->m_interface_id] = vector<int>();
            for (int j = 0; j < (int)m_interfaces.size(); j++)
            {
                if (j == i)
                    continue;
                m_topology[m_interfaces[i]->m_interface_id].push_back(m_interfaces[j]->m_interface_id);
            }
        }

        interconnect_controller = new TripleBusController(getSubMap(STRINGIFY(interconnect_controller)), 
                                                          &m_interfaces, &m_lower_level_ids, parent_name + "." + name);
    }

    TripleBus::~TripleBus()
    {
    }
}