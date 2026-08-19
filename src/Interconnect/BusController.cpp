/*
 * File  :      BusController.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 20, 2022
 */

#include "../../header/Interconnect/BusController.h"

using namespace std;
namespace octopus
{
    BusController::BusController(ParametersMap map, vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids,
                                 string pname, string config_path, string name) : Configurable(map, config_path, name, pname)
    {
        m_interfaces = interfaces;
        m_lower_level_ids = lower_level_ids;

        m_request_latency = std::get<int>(parameters.at(STRINGIFY(m_request_latency)).value);
        m_response_latency = std::get<int>(parameters.at(STRINGIFY(m_response_latency)).value);

        dprint = new DebugPrint(getSubMap(STRINGIFY(dprint)), name, parent_name + "." + name);
    }

    BusController::~BusController()
    {
        for (Arbiter *arbiter : m_arbiters)
        {
            delete arbiter;
        }
    }

    bool BusController::broadcast(Message &msg, MessageType type)
    {
        // Back-pressure applies ONLY to demand requests (GetS/GetM). Service traffic
        // -- writebacks (PutM), back-invalidations, data responses -- must ALWAYS be
        // accepted (they are bounded upstream by MSHR/PWB and drain the queues);
        // holding them would starve the traffic that unblocks the pipeline and
        // deadlock. So:
        //   * demand snoop  -> deliver ATOMICALLY (all-or-nothing): verify every
        //     receiver can accept, else deliver to none and report failure so the
        //     caller holds the elected message and retries next cycle. Never a
        //     partial delivery (which would silently break coherence) or a drop.
        //   * service/response -> force into every receiver unconditionally.
        if (msg.isDemandRequest())
        {
            for (int i = 0; i < (int)m_interfaces->size(); i++)
                if (!m_interfaces->at(i)->canAcceptRX(type))
                    return false;
        }

        Logger::getLogger()->updateRequest(msg.msg_id, Logger::EntryId::REQ_BUS_CHECKPOINT);

        for (int i = 0; i < (int)m_interfaces->size(); i++)
            m_interfaces->at(i)->pushMessage2RX(msg, type);

        return true;
    }

    void BusController::send(Message &msg, MessageType type)
    {
        Logger::getLogger()->updateRequest(msg.msg_id, Logger::EntryId::RESP_BUS_CHECKPOINT);

        for (int i = 0; i < (int)msg.to.size(); i++)
        {
            int destination_index = -1;
            for (int j = 0; j < (int)m_interfaces->size(); j++)
            {
                if (m_interfaces->at(j)->m_interface_id == msg.to[i])
                {
                    destination_index = j;
                    break;
                }
            }

            if (destination_index == -1)
            {
                cout << "BusController: Wrong destination" << endl;
                exit(0);
            }

            if (!m_interfaces->at(destination_index)->pushMessage2RX(msg, type))
            {
                cout << "BusController: full buffer" << endl;
                exit(0);
            }
        }
    }
}