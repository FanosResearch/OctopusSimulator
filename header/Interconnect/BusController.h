/*
 * File  :      BusController.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 10, 2022
 */

#ifndef _BUS_CONTROLLER_H
#define _BUS_CONTROLLER_H

#include "BusInterface.h"
#include "Configurable.h"
#include "DebugPrint.h"
#include "CommunicationInterface.h"

#include "Arbiter.h"
#include "TDMArbiter.h"
#include "FCFSArbiter.h"
#include "RRArbiter.h"

#include "Logger.h"

#include <vector>
#include <map>
#include <iostream>

using namespace std;

namespace octopus
{
    class BusController : public Configurable
    {
    public:
        enum class BusType
        {
            RequestBus = 0,
            ResponseBus,
        };

    protected:
        vector<CommunicationInterface *> *m_interfaces;
        vector<int> *m_lower_level_ids;
        // Arbiter candidates = every agent that transmits on this bus (all interface
        // ids: lower-level L1s AND the upper-level LLC). Owner-matched arbiters
        // (RR/TDM) need the full set, else LLC-sourced traffic (owner = LLC id) can
        // never be elected and starves. Populated by SplitBusController.
        vector<int> m_arbiter_candidate_ids;
        vector<Arbiter *> m_arbiters;
        DebugPrint* dprint;

        int m_request_latency;
        int m_response_latency;

        // Design B: true for the LLC<->DRAM bus, so its crossings log as MEM_BUS
        // rather than the L1<->LLC REQ_BUS/RESP_BUS. Set via setMemBus() at build.
        bool m_is_mem_bus = false;

        // LLC banks on this bus (docs/LLC_Banking_and_Partitioning_Design.md s4.6, B6): a broadcast
        // skips a bank it does not name, which would drop it on arrival anyway, so a full non-home
        // bank does not hold a demand request. Empty = one LLC, every interface takes part.
        std::vector<int> m_llc_banks;
        bool skipsInterface(const Message &msg, int interface_id) const;

        virtual bool broadcast(Message &msg, MessageType type = MessageType::REQUEST);
        virtual void send(Message &msg, MessageType type = MessageType::DATA_RESPONSE);

    public:
        void setMemBus() { m_is_mem_bus = true; }
        void setLLCBanks(const std::vector<int> &ids) { m_llc_banks = ids; }

        BusController(ParametersMap map, vector<CommunicationInterface *> *interfaces, vector<int> *lower_level_ids,
                      string pname = "",
                      string config_path = string(CONFIGURATION_PATH) + string(INTERCONNECT),
                      string name = STRINGIFY(BusController));
        ~BusController();

        virtual void busStep(uint64_t cycle_number){};
    };
}

#endif /* _BUS_CONTROLLER_H */
