/*
 * File  :      MemHazard.h
 * Read-after-write hazard detector for the LLC <-> memory path (diagnostic only).
 *
 * Enabled by OCTOPUS_MEM_HAZARD=1 (or =fatal: exit 2 at the first hazard); otherwise every hook
 * returns at once and nothing changes.
 * When the LLC sends a memory read it records how many writes to that line it has sent so far.
 * The memory controller then reports the read if it ARRIVES before those writes have arrived
 * (NET: overtaken in the interconnect) or COMPLETES before they were performed (MC: served
 * ahead of an older write to the same line inside the controller). Counts print at exit.
 */
#ifndef _MEM_HAZARD_H
#define _MEM_HAZARD_H
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace octopus
{
    class MemHazard
    {
    public:
        static bool on()
        {
            static const bool v = [](){ const char *e = getenv("OCTOPUS_MEM_HAZARD"); return e && *e && *e != '0'; }();
            return v;
        }
        static void writeSent(uint64_t addr, uint64_t msg_id = 0, uint64_t cycle = 0) { if (on()) { Line &l = get().m_lines[line(addr)]; l.sent++; l.last_w_id = msg_id; l.last_w_cycle = cycle; } }
        static void writeArrived(uint64_t addr)   { if (on()) get().m_lines[line(addr)].arrived++; }
        static void writeDelivered(uint64_t addr) { if (on()) get().m_lines[line(addr)].delivered++; }
        // the sender took a pending write back out of its TX buffer and used it as the fill
        // (CacheController_End2End rollback): the write is complete as far as ordering goes
        static void writeForwarded(uint64_t addr)
        {
            if (!on()) return;
            Line &l = get().m_lines[line(addr)];
            l.delivered++; l.arrived++; l.performed++; get().m_forwarded++;
        }
        static void readDelivered(uint64_t msg_id, uint64_t addr)
        {
            if (!on()) return;
            MemHazard &h = get();
            auto r = h.m_reads.find(key(msg_id, addr));
            if (r == h.m_reads.end()) return;
            if (h.m_lines[line(addr)].delivered < r->second) h.m_bus++;
        }
        static void writePerformed(uint64_t addr) { if (on()) get().m_lines[line(addr)].performed++; }
        static void readSent(uint64_t msg_id, uint64_t addr, uint64_t cycle = 0)
        {
            if (!on()) return;
            MemHazard &h = get();
            h.m_reads[key(msg_id, addr)] = h.m_lines[line(addr)].sent;
            h.m_read_cycle[key(msg_id, addr)] = cycle;
            h.m_reads_sent++;
        }
        static void readArrived(uint64_t msg_id, uint64_t addr, uint64_t cycle)
        {
            if (!on()) return;
            MemHazard &h = get();
            auto r = h.m_reads.find(key(msg_id, addr));
            if (r == h.m_reads.end()) return;
            const Line &l = h.m_lines[line(addr)];
            if (l.arrived < r->second) h.report("NET", msg_id, addr, cycle, r->second - l.arrived, h.m_net);
        }
        // the LLC filled a memory read from a write memory has not acknowledged yet
        static void llcForwarded() { if (on()) get().m_llc_forwarded++; }
        // the memory controller answered the read from a write it holds (read forwarding)
        static void readForwarded(uint64_t msg_id, uint64_t addr)
        {
            if (!on()) return;
            MemHazard &h = get();
            h.m_reads.erase(key(msg_id, addr));
            h.m_mc_forwarded++;
        }
        static void readCompleted(uint64_t msg_id, uint64_t addr, uint64_t cycle)
        {
            if (!on()) return;
            MemHazard &h = get();
            auto r = h.m_reads.find(key(msg_id, addr));
            if (r == h.m_reads.end()) return;
            const Line &l = h.m_lines[line(addr)];
            if (l.performed < r->second) h.report("MC", msg_id, addr, cycle, r->second - l.performed, h.m_mc);
            h.m_reads.erase(r);
        }
        ~MemHazard()
        {
            if (!on()) return;
            uint64_t writes = 0;
            for (auto &kv : m_lines) writes += kv.second.sent;
            fprintf(stderr, "[MEM-HAZARD] summary: reads %llu, writes %llu, NET overtakes %llu, MC overtakes %llu, of NET on the link %llu, writes forwarded at the sender %llu, reads forwarded at memory %llu, reads forwarded at the LLC %llu\n",
                    (unsigned long long)m_reads_sent, (unsigned long long)writes, (unsigned long long)m_net, (unsigned long long)m_mc, (unsigned long long)m_bus, (unsigned long long)m_forwarded, (unsigned long long)m_mc_forwarded, (unsigned long long)m_llc_forwarded);
        }

    private:
        struct Line { uint64_t sent = 0, arrived = 0, performed = 0, last_w_id = 0, last_w_cycle = 0, delivered = 0; };
        std::unordered_map<uint64_t, uint64_t> m_read_cycle;
        std::unordered_map<uint64_t, Line> m_lines;
        std::unordered_map<uint64_t, uint64_t> m_reads;   // (msg_id, line) -> writes to the line sent before this read
        uint64_t m_reads_sent = 0, m_net = 0, m_mc = 0, m_bus = 0, m_forwarded = 0, m_mc_forwarded = 0, m_llc_forwarded = 0;

        static MemHazard &get() { static MemHazard h; return h; }
        static uint64_t line(uint64_t addr) { return addr & ~63ULL; }
        static uint64_t key(uint64_t msg_id, uint64_t addr) { return (msg_id * 0x9E3779B97F4A7C15ULL) ^ line(addr); }
        void report(const char *where, uint64_t msg_id, uint64_t addr, uint64_t cycle, uint64_t writes_ahead, uint64_t &count)
        {
            static const bool fatal = [](){ const char *e = getenv("OCTOPUS_MEM_HAZARD"); return e && std::string(e) == "fatal"; }();
            if (++count <= 10 || fatal)
            {
                const Line &l = m_lines[line(addr)];
                fprintf(stderr, "[MEM-HAZARD] %s: read msg %llu line 0x%llx sent at %llu, at memory cycle %llu, ahead of %llu older write(s); last write msg %llu sent at %llu\n",
                        where, (unsigned long long)msg_id, (unsigned long long)line(addr), (unsigned long long)m_read_cycle[key(msg_id, addr)], (unsigned long long)cycle,
                        (unsigned long long)writes_ahead, (unsigned long long)l.last_w_id, (unsigned long long)l.last_w_cycle);
                if (fatal) exit(2);
            }
        }
    };
}
#endif /* _MEM_HAZARD_H */
