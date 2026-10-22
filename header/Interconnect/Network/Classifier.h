/*
 * File  :      Classifier.h
 * Message -> class (virtual network). Specification: docs/NoC_Architecture_Spec.md §6.3.
 */
#ifndef _NET_CLASSIFIER_H
#define _NET_CLASSIFIER_H
#include "NetTypes.h"
#include <set>
#include <string>

namespace octopus { namespace net
{
    class Classifier
    {
    public:
        // num_classes: 3 (request/forward/response) or 2 (legacy: no data / data) for Octopus's own
        // protocols; 5 (A < B < C < D < E) or 7 (A_in < B < C < A_out < D_out < D_in < E) for TileLink,
        // where a class is a channel (docs/TileLink_Plan.md s5.3 I-C, spec s6.3).
        // home_ids: the LLC / directory homes, for the fallback rules.
        Classifier(int num_classes, const std::set<int> &home_ids);

        // The class of a message for destination `dst`, or -1 if no rule matches (why() says which).
        // Counts fallback-classified messages (spec §6.3: reported once per run).
        int classify(const Message &m, int dst) const;
        bool isHome(int id) const { return m_homes.count(id) > 0; }
        // Same, but fatal() on -1 with the message's fields printed.
        int classifyOrFatal(const Message &m, int dst) const;
        // why classify(m, dst) returned -1 (the fatal path's text)
        std::string why(const Message &m, int dst) const;

        // ---- TileLink (num_classes 5 or 7)
        // the memory-side endpoints: the LLC banks' memory ports and the memory controllers. A
        // TileLink message is on the LLC's outer link iff its source and its destination are both
        // memory-side (spec s6.3); only a 7-class network has outer classes.
        void setMemory(const std::set<int> &ids) { m_memory = ids; }
        bool isMemory(int id) const { return m_memory.count(id) > 0; }
        const std::set<int> &memory() const { return m_memory; }
        bool tileLink() const { return m_num_classes == 5 || m_num_classes == 7; }
        // the class of TileLink channel `channel` (1..5 = A..E) on the inner (outer = false) or the
        // outer link; -1 with *why set if that channel has no class on this network
        int tlClass(int channel, bool outer, std::string *why = nullptr) const;
        // the class names, for the verdicts and the hang dump
        std::string className(int c) const;
        static const char *channelName(int channel);

        int  numClasses() const { return m_num_classes; }
        uint64_t fallbackCount() const { return m_fallbacks; }
        static std::string describe(const Message &m, int dst);

    private:
        int m_num_classes;
        std::set<int> m_homes, m_memory;
        mutable uint64_t m_fallbacks = 0;
    };
}}
#endif /* _NET_CLASSIFIER_H */
