/*
 * File  :      Transcript.cpp
 * The beat transcript (plan §4).
 */
#include "tilelink/Transcript.h"
#include <cstdio>

namespace octopus { namespace tl
{
    Transcript::Transcript(std::ostream &out) : m_out(&out) { *m_out << header() << "\n"; }

    Transcript::Transcript(const std::string &path) : m_file(new std::ofstream(path)), m_out(m_file.get())
    {
        if (!*m_file) fatal("cannot open transcript file " + path);
        *m_out << header() << "\n";
    }

    Transcript::~Transcript() { flush(); }

    void Transcript::flush() { if (m_out) m_out->flush(); }

    const char *Transcript::header()
    {
        return "#cycle link ch opcode param size source sink address beat mask data corrupt denied";
    }

    std::string Transcript::line(uint64_t cycle, const std::string &link, Channel ch, const Msg &m, unsigned beat)
    {
        char buf[256];
        std::snprintf(buf, sizeof buf, "%llu %s %s %s %u %u %llu %llu 0x%llx %u ",
                      (unsigned long long)cycle, link.c_str(), channelName(ch), opcodeName(ch, m.opcode),
                      unsigned(m.param), unsigned(m.size), (unsigned long long)m.source, (unsigned long long)m.sink,
                      (unsigned long long)m.address, beat);
        std::string s = buf;
        // Tables 6-10: the mask is a wire field of A and B only; "-" where the wire has none.
        s += (hasMask(ch) ? m.maskHex(beat) : std::string("-")) + " " + m.dataHex(beat) + " " +
             (m.beatCorrupt(beat) ? "1" : "0") + " " + (m.denied ? "1" : "0");
        return s;
    }

    void Transcript::record(uint64_t cycle, const std::string &link, Channel ch, const Msg &m, unsigned beat)
    {
        *m_out << line(cycle, link, ch, m, beat) << "\n";
        m_lines++;
    }
}}
