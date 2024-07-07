/*
 * File  :      Mesh.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 27, 2023
 */

#include "../../header/Interconnect/Mesh.h"

namespace octopus
{
    Mesh::Mesh(ParametersMap map, string pname, string config_path, string name, bool init_interfaces) 
            : ClockedObj(0), Configurable(map, config_path, name, pname)
    {
        m_cycle = 1;
        m_cycle_edges = 1;

        //Parameters initialization
        vector<int> component_ids = std::get<vector<int>>(parameters.at(STRINGIFY(component_ids)).value);
        int buffers_max_size = std::get<int>(parameters.at(STRINGIFY(buffers_max_size)).value);
        m_clk_period = std::get<int>(parameters.at(STRINGIFY(m_clk_period)).value);
        vector<vector<int>> connection_matrix; //first column is the node id and the rest of columns are what each column is connected to
        for(int i = 0; i < component_ids.size(); i++)
            connection_matrix.push_back(std::get<vector<int>>(parameters.at(string(STRINGIFY(connection_matrix)) + "[" + to_string(i) + "]").value));

        //Constructor
        dprint = new DebugPrint(getSubMap(STRINGIFY(dprint)), name, parent_name + "." + name);

        if(init_interfaces)
        {
            for (auto id : component_ids)
            {   
                int i;
                for(i = 0; i < connection_matrix.size(); i++)
                {    
                    if(connection_matrix[i][0] == id)
                        break;
                }
                vector<int> link_ids(connection_matrix[i].begin()+1, connection_matrix[i].end());
                m_interfaces.push_back(new MeshInterface(id, buffers_max_size, link_ids));
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
            
            
            interconnect_controller = new MeshController(getSubMap(STRINGIFY(interconnect_controller)), &m_interfaces, 
                                                        &m_topology, parent_name + "." + name);
        }
    }
    
    Mesh::~Mesh()
    {
        for (int i = 0; i < (int)m_interfaces.size(); i++)
        {
            delete m_interfaces[i];
        }
        m_interfaces.clear();
    }
    
    CommunicationInterface* Mesh::getInterfaceFor(int id)
    {
        for(int i = 0; i < (int)m_interfaces.size(); i++)
        {
            if(m_interfaces[i]->m_interface_id == id)
                return m_interfaces[i];
        }
        return NULL;
    }
        
    void Mesh::cycleProcess()
    {
        if(m_cycle_edges % 2 == 1)    //Falling edge
            interconnect_controller->step(m_cycle++);
        
        m_cycle_edges++;
    }

    void Mesh::init()
    {
    }
}