/*
 * File  :      TrafficGenerator.h
 * An endpoint that injects synthetic packets and consumes what it receives (spec §18).
 */
#ifndef _NET_TRAFFIC_GENERATOR_H
#define _NET_TRAFFIC_GENERATOR_H
#include "ClockManager.h"
#include "Configurable.h"
#include "CommunicationInterface.h"
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace octopus { namespace net
{
    class TrafficGenerator : public ClockedObj, public Configurable
    {
    public:
        // others: the other endpoint ids (destinations for uniform/neighbour); transpose_dst: the
        // destination of a permutation pattern (transpose, bitcomp, bitrev, shuffle, tornado,
        // randperm), computed by the system from the topology (-1 = none)
        static bool isPermutation(const std::string &pattern);
        TrafficGenerator(ParametersMap map, int id, CommunicationInterface *ni, const std::vector<int> &others, int transpose_dst,
                         std::string pname = "", std::string config_path = std::string(CONFIGURATION_PATH),
                         std::string name = STRINGIFY(TrafficGenerator));
        void cycleProcess() override;
        void init() override {}
        bool done() const { return m_done; }
        uint64_t sent() const { return m_sent; }
        uint64_t received() const { return m_received; }
        uint64_t retries() const { return m_retries; }
        uint64_t outOfOrder() const { return m_ooo; }
        int id() const { return m_id; }

    private:
        int pickDestination();
        void makeMessage(Message &m, int dst);
        int m_id; CommunicationInterface *m_ni;
        std::vector<int> m_others; int m_transpose;
        std::string m_pattern; int m_rate, m_payload, m_cls, m_hotspot, m_packets; long m_cycles; int m_seed;
        long m_consume_after, m_start; int m_multicast, m_untagged;
        std::map<int, uint64_t> m_last_from;    // source -> last msg_id seen (ordering check)
        uint64_t m_ooo = 0;
        std::mt19937_64 m_rng;
        uint64_t m_cycle = 0, m_sent = 0, m_received = 0, m_retries = 0, m_next_id = 0;
        bool m_pending = false; Message m_pending_msg;
        std::string m_injection; int m_on_to_off = 0, m_off_to_on = 0; bool m_on = false;   // on/off bursty injection
        bool m_done = false;
    };
}}
#endif /* _NET_TRAFFIC_GENERATOR_H */
