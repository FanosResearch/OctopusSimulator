/*
 * File  :      Point2PointController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On May 23, 2022
 */

#include "../../header/Interconnect/Point2PointController.h"

using namespace std;
namespace octopus
{
    Point2PointController::Point2PointController(ParametersMap map, vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids,
            string pname, string config_path, string name) : BusController(map, interfaces, lower_level_ids, pname, config_path, name)
    {
        vector<int> *ids_ptr;
        vector<int> *candidates_id = new vector<int>(std::get<vector<int>>(parameters.at(STRINGIFY(candidates_id)).value));
        string arbiter_type = std::get<string>(parameters.at(STRINGIFY(arbiter_type)).value);

        if(candidates_id->empty())
            ids_ptr = m_lower_level_ids;
        else
            ids_ptr = candidates_id;
        
        if(arbiter_type == STRINGIFY(TDMArbiter))
            m_arbiters.push_back(new TDMArbiter(ids_ptr, m_request_latency + m_response_latency));
        else if(arbiter_type == STRINGIFY(FCFSArbiter))
            m_arbiters.push_back(new FCFSArbiter(ids_ptr, m_request_latency + m_response_latency));
        else if(arbiter_type == STRINGIFY(RRArbiter))
            m_arbiters.push_back(new RRArbiter(ids_ptr, m_request_latency + m_response_latency));
        else
        {
            cout << "Error: there is no matching arbiter" << endl;
            exit(0);
        }
        
        clk_in_slot_lower = 0;
        message_available_lower = false;
        
        vector<CommunicationInterface *> lower_interfaces;
        for(int i = 0; i < (int) lower_level_ids->size(); i++)
        {
            for(int j = 0; j < (int) interfaces->size(); j++)
            {
                if(interfaces->at(j)->m_interface_id == lower_level_ids->at(i))
                {
                    lower_interfaces.push_back(interfaces->at(j));
                    break;
                }
            }
        }
        BusInterface::getCongregatedBuffers(lower_interfaces, true, &buffers_lower);
        BusInterface::getCongregatedBuffers(lower_interfaces, false, &buffers_lower);

        vector<CommunicationInterface *> upper_interface;
        for(int i = 0; i < (int) interfaces->size(); i++)
        {
            bool found = false;
            for(int j = 0; j < (int) lower_level_ids->size(); j++)
            {
                if(interfaces->at(i)->m_interface_id == lower_level_ids->at(j))
                {
                    found = true;
                    break;
                }
            }
            if(found == false)
            {
                upper_interface.push_back(interfaces->at(i));
                break;
            }
        }
        BusInterface::getCongregatedBuffers(upper_interface, true, &buffers_upper);
        BusInterface::getCongregatedBuffers(upper_interface, false, &buffers_upper);
    }

    Point2PointController::~Point2PointController()
    {
    }

    void Point2PointController::busStep(uint64_t cycle_number)
    {
        lowerBusStep(cycle_number);
        upperBusStep(cycle_number);
    }

    void Point2PointController::lowerBusStep(uint64_t cycle_number)
    {
        if (clk_in_slot_lower == 0)
            message_available_lower = m_arbiters[(int)BusType::RequestBus]->elect(cycle_number, buffers_lower, &elected_msg_lower);
        else if (clk_in_slot_lower == (m_request_latency - 1))
        {
            if (message_available_lower)
                send(elected_msg_lower); //send from lower to upper
        }

        if (message_available_lower)
            clk_in_slot_lower = (clk_in_slot_lower + 1) % (m_request_latency);
        else
            clk_in_slot_lower = 0;
    }

    void Point2PointController::upperBusStep(uint64_t cycle_number)
    {
        if (clk_in_slot_upper == 0)
        {
            for(int i = 0; i < (int) buffers_upper.size(); i++)
            {
                if(buffers_upper[i]->size() > 0)
                {
                    message_available_upper = true;
                    elected_msg_upper = buffers_upper[i]->at(0);
                    buffers_upper[i]->erase(buffers_upper[i]->begin());
                    break;
                }
                message_available_upper = false;
            }
        }
        else if (clk_in_slot_upper == (m_response_latency - 1))
        {
            if (message_available_upper)
                send(elected_msg_upper); //send from upper to lower
        }

        if (message_available_upper)
            clk_in_slot_upper = (clk_in_slot_upper + 1) % (m_response_latency);
        else
            clk_in_slot_upper = 0;
    }
}