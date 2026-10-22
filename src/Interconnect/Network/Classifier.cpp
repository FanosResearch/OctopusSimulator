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
        if (num_classes != 2 && num_classes != 3 && num_classes != 5 && num_classes != 7)
            throw ConfigError("num_classes must be 2, 3, 5 or 7, got " + std::to_string(num_classes));
    }

    // TileLink (docs/TileLink_Plan.md s5.3 I-C, spec s6.3): a class is a channel. 5 classes, memory
    // not on the network: A < B < C < D < E. 7 classes, memory on the network: the inner and the
    // outer link get disjoint classes, ordered so that every consume-then-emit edge goes up:
    // A_in < B < C < A_out < D_out < D_in < E. The outer link is TL-UH (Get/Put and their
    // AccessAck[Data]): it has A and D only.
    int Classifier::tlClass(int channel, bool outer, std::string *why) const
    {
        auto no = [&](const std::string &w) { if (why) *why = w; return -1; };
        if (channel < 1 || channel > 5) return no("TileLink channel " + std::to_string(channel) + " is not A..E (1..5)");
        if (m_num_classes == 5)
        {
            if (outer) return no("a TileLink message on the LLC's outer (memory) link on a 5-class network (memory on the network needs num_classes 7)");
            return channel - 1;
        }
        if (m_num_classes == 7)
        {
            static const int inner[] = {0, 1, 2, 5, 6};   // A_in, B, C, D_in, E
            if (!outer) return inner[channel - 1];
            if (channel == 1) return 3;                   // A_out
            if (channel == 4) return 4;                   // D_out
            return no(std::string("TileLink channel ") + channelName(channel) + " on the LLC's outer (memory) link, which carries A and D only");
        }
        return no("a TileLink message on a " + std::to_string(m_num_classes) + "-class network (TileLink needs num_classes 5 or 7)");
    }

    int Classifier::classify(const Message &m, int dst) const
    {
        // TileLink and Octopus's own messages never share a network (no silent fallback)
        if (tileLink() || m.tl.channel != 0)
        {
            if (!tileLink() || m.tl.channel == 0) return -1;
            bool src_mem = isMemory(m.from), dst_mem = isMemory(dst);
            if (src_mem != dst_mem) return -1;            // between the inner and the outer link
            return tlClass(m.tl.channel, src_mem);
        }
        if (m_num_classes == 2)                       // legacy split, kind ignored (spec §6.3)
            return m.data == NULL ? 0 : 1;
        switch (m.kind)
        {
            case Message::K_GETS: case Message::K_GETM: case Message::K_PUTS: case Message::K_PUTM: case Message::K_PUTE: case Message::K_PUTO:
            case Message::K_DEMAND:
                return CLASS_REQUEST;
            case Message::K_FWD: case Message::K_INV:
            case Message::K_MEM_READ:          // a home's read of memory: consumed by a memory controller that answers with a fill (spec s25.3)
                return CLASS_FORWARD;
            case Message::K_ACK:
                // A home's PutAck travels with its forwards: the directory L1 FSM relies on a home's
                // Inv/Fwd and the later PutAck to the same cache arriving in order (SI_a/MI_a/II_a
                // handle Inv-then-PutAck only), which the legacy request link gave it. Acks are
                // consumed without injecting anything, so the forward class stays drainable (s6.3).
                return (m.data == NULL && m_homes.count(m.from)) ? CLASS_FORWARD : CLASS_RESPONSE;
            case Message::K_RESP: case Message::K_SUPPLY: case Message::K_SUPPLY_DEFERRED:
            case Message::K_FILL: case Message::K_FILL_ROLLBACK:
            case Message::K_WB_DATA: case Message::K_MEM_WRITE:
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

    std::string Classifier::why(const Message &m, int dst) const
    {
        if (tileLink() && m.tl.channel == 0)
            return "a message without a TileLink channel on a " + std::to_string(m_num_classes) + "-class (TileLink) network";
        if (!tileLink() && m.tl.channel != 0)
            return "a TileLink message on a " + std::to_string(m_num_classes) + "-class network (TileLink needs num_classes 5 or 7)";
        if (tileLink())
        {
            bool src_mem = isMemory(m.from), dst_mem = isMemory(dst);
            if (src_mem != dst_mem)
                return std::string("a TileLink message between a memory-side endpoint and an inner one (") + std::to_string(m.from) + (src_mem ? " is" : " is not") +
                       " memory-side, " + std::to_string(dst) + (dst_mem ? " is" : " is not") + "; memory_ids)";
            std::string w; tlClass(m.tl.channel, src_mem, &w);
            return w;
        }
        return "unclassifiable message";
    }

    int Classifier::classifyOrFatal(const Message &m, int dst) const
    {
        int c = classify(m, dst);
        if (c < 0) fatal(why(m, dst) + " " + describe(m, dst));
        return c;
    }

    const char *Classifier::channelName(int channel)
    {
        static const char *n[] = {"none", "A", "B", "C", "D", "E"};
        return (channel >= 0 && channel <= 5) ? n[channel] : "?";
    }
    std::string Classifier::className(int c) const
    {
        static const char *c2[] = {"REQUEST", "DATA"}, *c3[] = {"REQUEST", "FORWARD", "RESPONSE"},
                          *c5[] = {"A", "B", "C", "D", "E"}, *c7[] = {"A_in", "B", "C", "A_out", "D_out", "D_in", "E"};
        if (c < 0 || c >= m_num_classes) return std::to_string(c);
        switch (m_num_classes) { case 2: return c2[c]; case 3: return c3[c]; case 5: return c5[c]; default: return c7[c]; }
    }

    std::string Classifier::describe(const Message &m, int dst)
    {
        std::ostringstream o;
        o << "{msg_id " << m.msg_id << ", addr 0x" << std::hex << m.addr << std::dec
          << ", kind " << (int)m.kind << ", cv " << m.complementary_value << ", owner " << m.owner
          << ", from " << m.from << ", to " << dst << ", data " << (m.data != NULL ? "yes" : "no");
        if (m.tl.channel != 0)
            o << ", tl channel " << channelName(m.tl.channel) << " opcode " << (int)m.tl.opcode << " param " << (int)m.tl.param
              << " source " << m.tl.source << " sink " << m.tl.sink;
        o << "}";
        return o.str();
    }
}}
