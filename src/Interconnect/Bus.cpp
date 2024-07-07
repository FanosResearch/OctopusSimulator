/*
 * File  :      Bus.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 5, 2022
 */

#include "../../header/Interconnect/Bus.h"

namespace octopus
{
    Bus::Bus(ParametersMap map, vector<int>* candidates_id, string pname, string config_path, string name) 
            : ClockedObj(0), Configurable(map, config_path, name, pname)
    {
        m_bus_cycle = 1;
        m_bus_cycle_edges = 1;

        //Parameters initialization
        m_lower_level_ids = std::get<vector<int>>(parameters.at(STRINGIFY(m_lower_level_ids)).value);
        vector<int> upper_level_cache_ids = std::get<vector<int>>(parameters.at(STRINGIFY(upper_level_cache_ids)).value);
        string controller_type = std::get<string>(parameters.at(STRINGIFY(controller_type)).value);
        int buffers_max_size = std::get<int>(parameters.at(STRINGIFY(buffers_max_size)).value);
        m_clk_period = std::get<int>(parameters.at(STRINGIFY(m_clk_period)).value);

        //Constructor

        dprint = new DebugPrint(getSubMap(STRINGIFY(dprint)), name, parent_name + "." + name);

        for (auto id : upper_level_cache_ids)
            m_interfaces.push_back(new BusInterface(id, buffers_max_size));

        for (auto id : m_lower_level_ids)
            m_interfaces.push_back(new BusInterface(id, buffers_max_size));

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
        
        if(controller_type == "Point2Point")
            interconnect_controller = new Point2PointController(getSubMap(STRINGIFY(interconnect_controller)), 
                                                                &m_interfaces, &m_lower_level_ids, parent_name + "." + name);
        else if(controller_type == "Split")
            interconnect_controller = new SplitBusController(getSubMap(STRINGIFY(interconnect_controller)), 
                                                             &m_interfaces, &m_lower_level_ids, parent_name + "." + name);
        else if(controller_type == "Unified")
            interconnect_controller = new UnifiedBusController(getSubMap(STRINGIFY(interconnect_controller)), 
                                                               &m_interfaces, &m_lower_level_ids, parent_name + "." + name);
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