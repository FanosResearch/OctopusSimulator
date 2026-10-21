/*
 * File  :      Stats.h
 * TLStats.csv (plan §4): per channel beats, busy and stall cycles; per opcode the message
 * latency (first beat offered to last beat accepted, fed by the kernel) and the operation
 * latency (request first beat to response last beat, fed by the Monitor of T2 through
 * recordOperation); the outstanding-ID histogram, fed by whoever owns an ID pool.
 */
#ifndef _TL_STATS_H
#define _TL_STATS_H
#include "Channel.h"
#include <cstdint>
#include <map>
#include <string>

namespace octopus { namespace tl
{
    struct LatencyStats
    {
        uint64_t count = 0, sum = 0, max = 0, min = ~uint64_t(0);
        uint64_t bucket[24] = {0};   // bucket[b] = samples with floor(log2(latency+1)) == b
        void add(uint64_t latency);
        double mean() const { return count ? double(sum) / double(count) : 0.0; }
    };

    class Stats
    {
    public:
        // Per channel, from ChannelState::stats() at the end of the run.
        void setChannel(const std::string &link, Channel ch, const ChannelStats &s);
        // A message completed on (link, ch): latency = t_last_accept - t_first_offer.
        void recordMessage(const std::string &link, Channel ch, unsigned opcode, uint64_t latency);
        // An operation completed: request (link, ch, opcode) answered, latency from the
        // request's first beat offered to the response's last beat accepted.
        void recordOperation(const std::string &link, Channel ch, unsigned opcode, uint64_t latency);
        // One cycle observed with n IDs outstanding in `space` ("A", "C" or "sink") on link.
        void recordOutstanding(const std::string &link, const std::string &space, unsigned n);

        // Writes the CSV; fatal if the file cannot be opened. Sections, each with a header row:
        //   channel,link,ch,beats,busy,stall
        //   message,link,ch,opcode,count,mean,max,min
        //   operation,link,ch,opcode,count,mean,max,min
        //   outstanding,link,space,n,cycles
        void write(const std::string &path) const;
        std::string csv() const;

        const LatencyStats *message(const std::string &link, Channel ch, unsigned opcode) const;
        const LatencyStats *operation(const std::string &link, Channel ch, unsigned opcode) const;

    private:
        using OpKey = std::pair<std::string, std::pair<int, unsigned>>;   // (link, (ch, opcode))
        std::map<std::pair<std::string, int>, ChannelStats> m_channels;
        std::map<OpKey, LatencyStats> m_messages, m_operations;
        std::map<std::pair<std::string, std::string>, std::map<unsigned, uint64_t>> m_outstanding;
    };
}}
#endif /* _TL_STATS_H */
