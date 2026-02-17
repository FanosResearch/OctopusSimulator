/*
 * File  :      DirectController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Sept 15, 2022
 */

#include "../../header/Interconnect/DirectController.h"

using namespace std;
namespace ns3
{
    DirectController::DirectController(vector<CommunicationInterface *> *interfaces)
    {
        m_interfaces = interfaces;

        BusInterface::getCongregatedBuffers(*interfaces, true, &m_request_buffers);
        BusInterface::getCongregatedBuffers(*interfaces, false, &m_response_buffers);
    }

    bool DirectController::send(Message &msg, int destination, MessageType type)
    {
        // if (!m_interfaces->at(destination)->pushMessage2RX(msg, type))
        // {
        //     cout << "DirectController: full buffer" << endl;
        //     exit(0);
        // }
        return m_interfaces->at(destination)->pushMessage2RX(msg, type);
    }

    void DirectController::controllerStep(uint64_t cycle_number)
    {
        for (int i = 0; i < (int)m_request_buffers.size(); i++)
        {
            if (!m_request_buffers.at(i)->empty())
            {
                if (send(m_request_buffers.at(i)->at(0), i ^ 1, MessageType::REQUEST))
                {
                    m_request_buffers.at(i)->erase(m_request_buffers.at(i)->begin());
                    break;
                }
            }
        }

        for (int i = 0; i < (int)m_response_buffers.size(); i++)
        {
            if (!m_response_buffers.at(i)->empty())
            {
                if (send(m_response_buffers.at(i)->at(0), i ^ 1, MessageType::DATA_RESPONSE))
                {
                    m_response_buffers.at(i)->erase(m_response_buffers.at(i)->begin());
                    break;
                }
            }
        }
    }
}