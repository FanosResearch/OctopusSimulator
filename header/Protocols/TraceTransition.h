/*
 * TraceTransition.h -- SCRATCH diagnostic. Per-controller FSM transition trace,
 * filtered to the block addresses in TRACE_ADDR (comma-separated hex). One line per
 * transition: cycle, tag (L1/LLC), controller id, current state, event, next state, actions.
 * Remove before committing.
 */
#ifndef _TRACE_TRANSITION_H
#define _TRACE_TRANSITION_H

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>

namespace octopus
{
    inline const std::vector<uint64_t> &traceAddrList()
    {
        static const std::vector<uint64_t> blks = [] {
            std::vector<uint64_t> v;
            if (const char *ta = getenv("TRACE_ADDR")) {
                std::string s(ta); size_t p = 0;
                for (;;) { size_t c = s.find(',', p);
                    std::string t = s.substr(p, c == std::string::npos ? std::string::npos : c - p);
                    if (!t.empty()) v.push_back(strtoull(t.c_str(), NULL, 16));
                    if (c == std::string::npos) break; p = c + 1; }
            }
            return v;
        }();
        return blks;
    }

    inline bool traceHit(uint64_t addr, uint64_t block_size)
    {
        static const bool trace_all = (getenv("TRACE_ALL") != NULL);
        if (trace_all) return true;
        const std::vector<uint64_t> &tl = traceAddrList();
        if (tl.empty()) return false;
        uint64_t bm = ~(uint64_t)(block_size - 1);
        for (uint64_t a : tl) if ((a & bm) == (addr & bm)) return true;
        return false;
    }
}

#define TRACE_TRANSITION(TAG, ID, MSG, STATE, EVENT, NEXT, ACTIONS)                                  \
    do {                                                                                             \
        if (octopus::traceHit((MSG).addr, m_data_handler->getBlockSize())) {                         \
            std::cout << "[TR cyc=" << m_data_handler->getCycle() << " " << TAG                       \
                      << " id=" << (ID) << " a=0x" << std::hex << (MSG).addr << std::dec              \
                      << " st=" << (STATE) << " ev=" << (int)(EVENT)                                  \
                      << "(cv=" << (MSG).complementary_value << ",src=" << (int)(MSG).source          \
                      << ",own=" << (MSG).owner << ",d=" << ((MSG).data != NULL) << ") ->" << (NEXT)  \
                      << " act=";                                                                     \
            for (int _ac : (ACTIONS)) std::cout << _ac << ",";                                        \
            std::cout << std::endl;                                                                   \
        }                                                                                            \
    } while (0)

#endif
