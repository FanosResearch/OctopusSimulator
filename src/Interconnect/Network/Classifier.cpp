/*
 * File  :      Classifier.cpp
 * Message -> class. Specification: docs/NoC_Architecture_Spec.md §6.3.
 */
#include "../../../header/Interconnect/Network/Classifier.h"
#include <sstream>

namespace octopus { namespace net
{
    Classifier::Classifier(int num_classes, const std::set<int> &home_ids)
        : m_num_classes(num_classes), m_homes(home_ids)
    {
        if (num_classes != 2 && num_classes != 3)
            throw ConfigError("num_classes must be 2 or 3, got " + std::to_string(num_classes));
    }

    int Classifier::classify(const Message &m, int dst) const
    {
        if (m_num_classes == 2)                       // legacy split, kind ignored (spec §6.3)
            return m.data == NULL ? 0 : 1;
        switch (m.kind)
        {
            case Message::K_GETS: case Message::K_GETM: case Message::K_PUTS: case Message::K_PUTM:
            case Message::K_DEMAND:
                return CLASS_REQUEST;
            case Message::K_FWD: case Message::K_INV:
                return CLASS_FORWARD;
            case Message::K_ACK:
                // A home's PutAck travels with its forwards: the directory L1 FSM relies on a home's
                // Inv/Fwd and the later PutAck to the same cache arriving in order (SI_a/MI_a/II_a
                // handle Inv-then-PutAck only), which the legacy request link gave it. Acks are
                // consumed without injecting anything, so the forward class stays drainable (s6.3).
                return (m.data == NULL && m_homes.count(m.from)) ? CLASS_FORWARD : CLASS_RESPONSE;
            case Message::K_RESP: case Message::K_SUPPLY: case Message::K_SUPPLY_DEFERRED:
            case Message::K_FILL: case Message::K_FILL_ROLLBACK:
            case Message::K_WB_DATA: case Message::K_MEM_READ: case Message::K_MEM_WRITE:
            case Message::K_EVICT: case Message::K_MEM_WACK:
                return CLASS_RESPONSE;
            default: break;
        }
        // fallback rules for untagged messages
        m_fallbacks++;
        if (m.data != NULL) return CLASS_RESPONSE;
        if (m_homes.count(dst)) return CLASS_REQUEST;
        if (m_homes.count(m.from) || m_homes.count(m.owner)) return CLASS_FORWARD;
        m_fallbacks--;                                // not classified after all
        return -1;
    }

    int Classifier::classifyOrFatal(const Message &m, int dst) const
    {
        int c = classify(m, dst);
        if (c < 0) fatal("unclassifiable message " + describe(m, dst));
        return c;
    }

    std::string Classifier::describe(const Message &m, int dst)
    {
        std::ostringstream o;
        o << "{msg_id " << m.msg_id << ", addr 0x" << std::hex << m.addr << std::dec
          << ", kind " << (int)m.kind << ", cv " << m.complementary_value << ", owner " << m.owner
          << ", from " << m.from << ", to " << dst << ", data " << (m.data != NULL ? "yes" : "no") << "}";
        return o.str();
    }
}}
