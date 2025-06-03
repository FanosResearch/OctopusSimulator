/*
 * File  :      DirectInterconnect.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sept 15, 2022
 */

#include "../../header/Interconnect/DirectInterconnect.h"

namespace ns3
{
    DirectInterconnect::DirectInterconnect(int lower_id, int upper_id, int buffers_max_size) : ClockedObj(50)
    {
        m_interconnect_cycle = 1;
        m_interconnect_cycle_edges = 1;

        m_interfaces.push_back(new BusInterface(lower_id, buffers_max_size));
        m_interfaces.push_back(new BusInterface(upper_id, buffers_max_size));

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
        
        interconnect_controller = new DirectController(&m_interfaces);
    }

    DirectInterconnect::~DirectInterconnect()
    {
        for (int i = 0; i < (int)m_interfaces.size(); i++)
        {
            delete m_interfaces[i];
        }
        m_interfaces.clear();
    }
    
    CommunicationInterface* DirectInterconnect::getInterfaceFor(int id)
    {
        for(int i = 0; i < (int)m_interfaces.size(); i++)
        {
            if(m_interfaces[i]->m_interface_id == id)
                return m_interfaces[i];
        }
        return NULL;
    }
    
    void DirectInterconnect::cycleProcess()
    {
        if(m_interconnect_cycle_edges % 2 == 1)    //Falling edge
            interconnect_controller->controllerStep(m_interconnect_cycle++);
        
        m_interconnect_cycle_edges++;
    }

    void DirectInterconnect::init()
    {
    }
}