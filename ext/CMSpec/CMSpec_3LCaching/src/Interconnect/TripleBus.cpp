/*
 * File  :      TripleBus.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 16, 2022
 */

#include "../../header/Interconnect/TripleBus.h"

namespace ns3
{
    TripleBus::TripleBus(list<CacheXml> &lower_level_caches, list<CacheXml> &upper_level_caches, int buffers_max_size) : Bus()
    {
        for (list<CacheXml>::iterator iter = upper_level_caches.begin(); iter != upper_level_caches.end(); iter++)
        {
            CacheXml cache_info = *iter;
            m_interfaces.push_back(new TripleBusInterface(cache_info.GetCacheId(), buffers_max_size));
        }

        for (list<CacheXml>::iterator iter = lower_level_caches.begin(); iter != lower_level_caches.end(); iter++)
        {
            CacheXml cache_info = *iter;
            m_interfaces.push_back(new TripleBusInterface(cache_info.GetCacheId(), buffers_max_size));
            m_lower_level_ids.push_back(cache_info.GetCacheId());
        }

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

        interconnect_controller = new TripleBusController(&m_interfaces, &m_lower_level_ids);
    }

    TripleBus::~TripleBus()
    {
    }
}