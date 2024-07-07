/*
 * File  :      NoC.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Jan 1, 2024
 */

#include "../../header/Interconnect/NoC.h"

namespace octopus
{
    NoC::NoC(ParametersMap map, string pname, string config_path, string name) 
            : Mesh(map, pname, config_path, name, false)
    {
        //Parameters initialization
        vector<int> component_ids = std::get<vector<int>>(parameters.at(STRINGIFY(component_ids)).value);
        vector<int> switch_ids = std::get<vector<int>>(parameters.at(STRINGIFY(switch_ids)).value);
        int buffers_max_size = std::get<int>(parameters.at(STRINGIFY(buffers_max_size)).value);
        vector<vector<int>> connection_matrix; //first column is the node id and the rest of columns are what each column is connected to
        for(int i = 0; i < component_ids.size() + switch_ids.size(); i++)
            connection_matrix.push_back(std::get<vector<int>>(parameters.at(string(STRINGIFY(connection_matrix)) + "[" + to_string(i) + "]").value));

        //Constructor
        component_ids.insert(component_ids.end(), switch_ids.begin(), switch_ids.end());//append switch_ids to component_ids
        for (auto id : component_ids)
        {   
            int i;
            for(i = 0; i < connection_matrix.size(); i++)
            {    
                if(connection_matrix[i][0] == id)
                    break;
            }
            vector<int> link_ids(connection_matrix[i].begin()+1, connection_matrix[i].end());
            m_interfaces.push_back(new NoCInterface(id, buffers_max_size, link_ids, connection_matrix, switch_ids));
        }

        for (int i = 0; i < (int)m_interfaces.size(); i++)
        {
            int j;
            for (j = 0; j < (int)connection_matrix.size(); j++)
                if (m_interfaces[i]->m_interface_id == connection_matrix[j][0])
                    break;

            m_topology[m_interfaces[i]->m_interface_id] = 
                        vector<int>(connection_matrix[j].begin() + 1, connection_matrix[j].end());
        }
        
        for(int switch_id : switch_ids)
        {
            Switch s(switch_id);
            s.interface = (NoCInterface*)this->getInterfaceFor(s.id);

            m_switches.push_back(s);
        }
        
        interconnect_controller = new NoCController(getSubMap(STRINGIFY(interconnect_controller)), &m_interfaces, 
                                                    &m_topology, switch_ids, parent_name + "." + name);
    }
    
    NoC::~NoC()
    {
    }
    
    void NoC::cycleProcess()
    {
        Mesh::cycleProcess();

        if(m_cycle_edges % 2 == 0)
        {
            for(Switch s : m_switches)
            {
                Message msg;
                while(s.interface->peekMessage(&msg))
                {
                    s.interface->popFrontMessage();     //Remove the message from the rx buffer
                    s.interface->noc_pushMessage(msg);  //Send  the message to the tx buffer
                }
            }
        }
    }
}