/*
 * File  :      Bus.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 5, 2022
 */

#include "../../header/Interconnect/Bus.h"

namespace ns3
{
    Bus::Bus() : ClockedObj(50)
    {
        m_bus_cycle = 1;
        m_bus_cycle_edges = 1;
    }

    Bus::Bus(list<CacheXml> &lower_level_caches, list<CacheXml> &upper_level_caches, int buffers_max_size) : ClockedObj(50)
    {
        m_bus_cycle = 1;
        m_bus_cycle_edges = 1;

        for (list<CacheXml>::iterator iter = upper_level_caches.begin(); iter != upper_level_caches.end(); iter++)
        {
            CacheXml cache_info = *iter;
            m_interfaces.push_back(new BusInterface(cache_info.GetCacheId(), buffers_max_size));
        }

        for (list<CacheXml>::iterator iter = lower_level_caches.begin(); iter != lower_level_caches.end(); iter++)
        {
            CacheXml cache_info = *iter;
            m_interfaces.push_back(new BusInterface(cache_info.GetCacheId(), buffers_max_size));
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
        
        // interconnect_controller = new TripleBusController(&m_interfaces, &m_lower_level_ids);
        interconnect_controller = new SplitBusController(&m_interfaces, &m_lower_level_ids);
        // interconnect_controller = new UnifiedBusController(&m_interfaces, &m_lower_level_ids);
    }
    
    Bus::Bus(list<CacheXml> &lower_level_caches, int upper_level_id, int buffers_max_size, vector<int>* candidates_id) : ClockedObj(50)
    {
        m_bus_cycle = 1;
        m_bus_cycle_edges = 1;

        m_interfaces.push_back(new BusInterface(upper_level_id, buffers_max_size));

        for (list<CacheXml>::iterator iter = lower_level_caches.begin(); iter != lower_level_caches.end(); iter++)
        {
            CacheXml cache_info = *iter;
            m_interfaces.push_back(new BusInterface(cache_info.GetCacheId(), buffers_max_size));
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
        
        // interconnect_controller = new SplitBusController(&m_interfaces, &m_lower_level_ids);
        // interconnect_controller = new UnifiedBusController(&m_interfaces, &m_lower_level_ids);
        interconnect_controller = new Point2PointController(&m_interfaces, &m_lower_level_ids, candidates_id);
    }

    Bus::~Bus()
    {
        for (int i = 0; i < (int)m_interfaces.size(); i++)
        {
            delete m_interfaces[i];
        }
        m_interfaces.clear();
    }
    
    CommunicationInterface* Bus::getInterfaceFor(int id)
    {
        for(int i = 0; i < (int)m_interfaces.size(); i++)
        {
            if(m_interfaces[i]->m_interface_id == id)
                return m_interfaces[i];
        }
        return NULL;
    }
    
    vector<int>* Bus::getLowerLevelIds()
    {
        return &m_lower_level_ids;
    }
    
    void Bus::cycleProcess()
    {
        if(m_bus_cycle_edges % 2 == 1)    //Falling edge
            interconnect_controller->busStep(m_bus_cycle++);
        
        m_bus_cycle_edges++;
    }

    void Bus::init()
    {
    }
}