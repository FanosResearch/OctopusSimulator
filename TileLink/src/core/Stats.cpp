/*
 * File  :      Stats.cpp
 * TLStats.csv (plan §4).
 */
#include "tilelink/Stats.h"
#include <fstream>
#include <sstream>

namespace octopus { namespace tl
{
    void LatencyStats::add(uint64_t latency)
    {
        count++;
        sum += latency;
        if (latency > max) max = latency;
        if (latency < min) min = latency;
        unsigned b = 0;
        for (uint64_t v = latency + 1; v > 1; v >>= 1) b++;
        bucket[b < 24 ? b : 23]++;
    }

    void Stats::setChannel(const std::string &link, Channel ch, const ChannelStats &s)
    {
        m_channels[{link, static_cast<int>(ch)}] = s;
    }
    void Stats::recordMessage(const std::string &link, Channel ch, unsigned opcode, uint64_t latency)
    {
        m_messages[{link, {static_cast<int>(ch), opcode}}].add(latency);
    }
    void Stats::recordOperation(const std::string &link, Channel ch, unsigned opcode, uint64_t latency)
    {
        m_operations[{link, {static_cast<int>(ch), opcode}}].add(latency);
    }
    void Stats::recordOutstanding(const std::string &link, const std::string &space, unsigned n)
    {
        m_outstanding[{link, space}][n]++;
    }

    const LatencyStats *Stats::message(const std::string &link, Channel ch, unsigned opcode) const
    {
        auto it = m_messages.find({link, {static_cast<int>(ch), opcode}});
        return it == m_messages.end() ? nullptr : &it->second;
    }
    const LatencyStats *Stats::operation(const std::string &link, Channel ch, unsigned opcode) const
    {
        auto it = m_operations.find({link, {static_cast<int>(ch), opcode}});
        return it == m_operations.end() ? nullptr : &it->second;
    }

    std::string Stats::csv() const
    {
        std::ostringstream o;
        o << "channel,link,ch,beats,busy,stall\n";
        for (auto &kv : m_channels)
            o << "channel," << kv.first.first << "," << channelName(static_cast<Channel>(kv.first.second)) << ","
              << kv.second.beats << "," << kv.second.busy << "," << kv.second.stall << "\n";
        auto latencies = [&](const char *section, const std::map<OpKey, LatencyStats> &m)
        {
            o << section << ",link,ch,opcode,count,mean,max,min\n";
            for (auto &kv : m)
            {
                Channel ch = static_cast<Channel>(kv.first.second.first);
                o << section << "," << kv.first.first << "," << channelName(ch) << "," << opcodeName(ch, kv.first.second.second)
                  << "," << kv.second.count << "," << kv.second.mean() << "," << kv.second.max << ","
                  << (kv.second.count ? kv.second.min : 0) << "\n";
            }
        };
        latencies("message", m_messages);
        latencies("operation", m_operations);
        o << "outstanding,link,space,n,cycles\n";
        for (auto &kv : m_outstanding)
            for (auto &h : kv.second)
                o << "outstanding," << kv.first.first << "," << kv.first.second << "," << h.first << "," << h.second << "\n";
        return o.str();
    }

    void Stats::write(const std::string &path) const
    {
        std::ofstream f(path);
        if (!f) fatal("cannot open statistics file " + path);
        f << csv();
    }
}}
