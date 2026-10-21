/*
 * File  :      Transcript.h
 * The beat transcript (plan §4): one line per ACCEPTED beat, the golden format for tests.
 *   cycle link ch opcode param size source sink address beat mask data corrupt denied
 * opcode as its name, address and mask as hex, data as hex or "-" in mode None. The first
 * line is a "#" header naming the columns. Deterministic: the kernel records channels in
 * link order, then channel order, within a cycle.
 */
#ifndef _TL_TRANSCRIPT_H
#define _TL_TRANSCRIPT_H
#include "Msg.h"
#include <fstream>
#include <memory>
#include <ostream>
#include <string>

namespace octopus { namespace tl
{
    class Transcript
    {
    public:
        explicit Transcript(std::ostream &out);          // write to a stream the caller owns
        explicit Transcript(const std::string &path);    // write to a file; fatal if it cannot open
        ~Transcript();

        void record(uint64_t cycle, const std::string &link, Channel ch, const Msg &m, unsigned beat);
        uint64_t lines() const { return m_lines; }
        void flush();

        static const char *header();
        static std::string line(uint64_t cycle, const std::string &link, Channel ch, const Msg &m, unsigned beat);

    private:
        std::unique_ptr<std::ofstream> m_file;
        std::ostream *m_out;
        uint64_t m_lines = 0;
    };
}}
#endif /* _TL_TRANSCRIPT_H */
