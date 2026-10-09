/*
 * File  :      TrafficGenerator.cpp
 * Specification: docs/NoC_Architecture_Spec.md §18.
 */
#include "../../../header/Interconnect/Network/TrafficGenerator.h"
#include "../../../header/Interconnect/Network/NetTypes.h"
#include <algorithm>

namespace octopus { namespace net
{
    TrafficGenerator::TrafficGenerator(ParametersMap map, int id, CommunicationInterface *ni, const std::vector<int> &others, int transpose_dst,
                                       std::string pname, std::string config_path, std::string name)
        : ClockedObj(0), Configurable(map, config_path, name, pname), m_id(id), m_ni(ni), m_others(others), m_transpose(transpose_dst)
    {
        m_clk_period = std::get<int>(parameters.at(STRINGIFY(m_clk_period)).value);
        m_pattern = std::get<std::string>(parameters.at(STRINGIFY(pattern)).value);
        m_rate = std::get<int>(parameters.at(STRINGIFY(rate)).value);
        m_payload = std::get<int>(parameters.at(STRINGIFY(payload_bytes)).value);
        m_cls = std::get<int>(parameters.at(STRINGIFY(cls)).value);
        m_hotspot = std::get<int>(parameters.at(STRINGIFY(hotspot_id)).value);
        m_packets = std::get<int>(parameters.at(STRINGIFY(packets)).value);
        m_cycles = std::get<int>(parameters.at(STRINGIFY(cycles)).value);
        m_seed = std::get<int>(parameters.at(STRINGIFY(seed)).value);
        m_consume_after = std::get<int>(parameters.at(STRINGIFY(consume_after)).value);
        m_multicast = std::get<int>(parameters.at(STRINGIFY(multicast)).value);
        m_untagged = std::get<int>(parameters.at(STRINGIFY(untagged)).value);
        m_start = std::get<int>(parameters.at(STRINGIFY(start)).value);
        if (m_multicast < 1) throw ConfigError("generator multicast must be >= 1");
        if (m_pattern != "uniform" && m_pattern != "hotspot" && m_pattern != "transpose" && m_pattern != "neighbour" && m_pattern != "none")
            throw ConfigError("generator " + std::to_string(id) + ": unknown pattern '" + m_pattern + "'");
        if (m_rate < 0 || m_rate > 1000) throw ConfigError("generator rate is packets per 1000 ticks, 0..1000");
        m_rng.seed((uint64_t)m_seed * 1000003ull + (uint64_t)id);
        ClockManager::getClockManager()->registerCLKTrigger(this);
        if (m_pattern == "none" || m_rate == 0 || m_packets == 0) { m_done = true; ClockManager::getClockManager()->stopClock(this); }
    }

    int TrafficGenerator::pickDestination()
    {
        if (m_others.empty()) return -1;
        if (m_pattern == "hotspot") return m_hotspot == m_id ? m_others[0] : m_hotspot;
        if (m_pattern == "transpose") return m_transpose >= 0 ? m_transpose : m_others[0];
        if (m_pattern == "neighbour")
        {
            auto it = std::upper_bound(m_others.begin(), m_others.end(), m_id);
            return it == m_others.end() ? m_others[0] : *it;
        }
        return m_others[m_rng() % m_others.size()];
    }
    void TrafficGenerator::makeMessage(Message &m, int dst)
    {
        m.msg_id = (uint64_t)m_id * 1000000000ull + (++m_next_id);
        m.addr = m.msg_id * 64; m.cycle = m_cycle; m.owner = (uint16_t)m_id; m.from = (uint16_t)m_id;
        m.to.clear(); m.to.push_back((uint16_t)dst);
        for (int k = 1; k < m_multicast && k < (int)m_others.size() + 1; k++)       // further destinations, in id order, skipping dst
        {
            for (int o : m_others) if (o != dst && std::find(m.to.begin(), m.to.end(), (uint16_t)o) == m.to.end()) { m.to.push_back((uint16_t)o); break; }
        }
        m.kind = m_untagged ? Message::K_UNKNOWN : (m_cls == 0 ? Message::K_GETS : (m_cls == 1 ? Message::K_FWD : Message::K_RESP));
        m.complementary_value = 0;
        if (m_payload > 0) { std::vector<uint8_t> buf(m_payload, 0xA5); m.copy(buf.data(), (uint16_t)m_payload); }
    }

    void TrafficGenerator::cycleProcess()
    {
        m_cycle++;
        // consume everything delivered
        Message in;
        bool consuming = m_consume_after >= 0 && (long)m_cycle > m_consume_after;
        while (consuming && m_ni->peekMessage(&in))
        {
            m_ni->popFrontMessage(); m_received++;
            uint64_t &last = m_last_from[in.from];
            if (in.msg_id < last) m_ooo++;
            last = in.msg_id;
        }
        if (m_done) return;
        // retry a refused message first
        if (m_pending)
        {
            if (m_ni->pushMessage(m_pending_msg, m_cycle)) { m_pending = false; m_sent++; }
            else m_retries++;
        }
        else if ((long)m_cycle >= m_start && (m_packets < 0 || (int)m_sent < m_packets) && (m_cycles < 0 || (long)m_cycle <= m_cycles))
        {
            if ((int)(m_rng() % 1000) < m_rate)
            {
                int dst = pickDestination();
                if (dst >= 0)
                {
                    makeMessage(m_pending_msg, dst);
                    if (m_ni->pushMessage(m_pending_msg, m_cycle)) m_sent++;
                    else { m_pending = true; m_retries++; }
                }
            }
        }
        bool budget_done = (m_packets >= 0 && (int)m_sent >= m_packets) || (m_cycles >= 0 && (long)m_cycle > m_cycles);
        if (budget_done && !m_pending)
        {
            m_done = true;
            ClockManager::getClockManager()->stopClock(this);
        }
    }
}}
