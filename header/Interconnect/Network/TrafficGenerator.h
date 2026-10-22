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
        // destination for the transpose pattern (-1 = none)
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
        // TileLink mode (key tl_mix(vi)): a memory-side generator sends on the outer link, A and D only
        void setMemorySide(bool m) { m_memory_side = m; }
        bool tileLink() const { return !m_tl_mix.empty(); }

    private:
        int pickDestination();
        void makeMessage(Message &m, int dst);
        int m_id; CommunicationInterface *m_ni;
        std::vector<int> m_others; int m_transpose;
        std::string m_pattern; int m_rate, m_payload, m_cls, m_hotspot, m_packets; long m_cycles; int m_seed;
        long m_consume_after, m_start; int m_multicast, m_untagged;
        std::vector<int> m_tl_mix;              // TileLink mode: weights of channels A..E; empty = off
        bool m_memory_side = false;
        int  m_pending_ch = 0;                  // TileLink: the channel holding a TX reservation for the pending message
        int  pickChannel();
        std::map<int, uint64_t> m_last_from;    // source -> last msg_id seen (ordering check)
        uint64_t m_ooo = 0;
        std::mt19937_64 m_rng;
        uint64_t m_cycle = 0, m_sent = 0, m_received = 0, m_retries = 0, m_next_id = 0;
        bool m_pending = false; Message m_pending_msg;
        bool m_done = false;
    };
}}
#endif /* _NET_TRAFFIC_GENERATOR_H */
