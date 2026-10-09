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
        // num_classes: 3 (request/forward/response) or 2 (legacy: no data / data).
        // home_ids: the LLC / directory homes, for the fallback rules.
        Classifier(int num_classes, const std::set<int> &home_ids);

        // The class of a message for destination `dst`, or -1 if no rule matches.
        // Counts fallback-classified messages (spec §6.3: reported once per run).
        int classify(const Message &m, int dst) const;
        // Same, but fatal() on -1 with the message's fields printed.
        int classifyOrFatal(const Message &m, int dst) const;

        int  numClasses() const { return m_num_classes; }
        uint64_t fallbackCount() const { return m_fallbacks; }
        static std::string describe(const Message &m, int dst);

    private:
        int m_num_classes;
        std::set<int> m_homes;
        mutable uint64_t m_fallbacks = 0;
    };
}}
#endif /* _NET_CLASSIFIER_H */
