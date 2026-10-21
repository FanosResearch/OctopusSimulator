/*
 * File  :      Msg.cpp
 * Beat layout (§4), mask derivation (§4.5) and transcript formatting of one message.
 */
#include "tilelink/Msg.h"
#include <cstdio>

namespace octopus { namespace tl
{
    bool parseDataMode(const std::string &s, DataMode &out)
    {
        if (s == "none")  { out = DataMode::None;  return true; }
        if (s == "stamp") { out = DataMode::Stamp; return true; }
        if (s == "full")  { out = DataMode::Full;  return true; }
        return false;
    }
    const char *dataModeName(DataMode m)
    {
        switch (m) { case DataMode::None: return "none"; case DataMode::Stamp: return "stamp"; default: return "full"; }
    }

    bool MsgHeader::operator==(const MsgHeader &o) const
    {
        return channel == o.channel && opcode == o.opcode && param == o.param && size == o.size &&
               source == o.source && sink == o.sink && address == o.address && denied == o.denied;
    }

    Msg::Msg(Channel ch, unsigned op, unsigned sz, unsigned width, DataMode mode)
    {
        channel = ch;
        opcode = uint8_t(op);
        size = uint8_t(sz);
        layout(width, mode);
    }
    MsgPtr Msg::make(Channel ch, unsigned op, unsigned sz, unsigned width, DataMode mode)
    {
        return std::make_shared<Msg>(ch, op, sz, width, mode);
    }

    void Msg::layout(unsigned width, DataMode mode)
    {
        w = width;
        data_mode = mode;
        unsigned n = beats();
        mask.assign(size_t(n) * w, 0);
        corrupt.assign(n, 0);
        data.clear();
        stamp.clear();
        // Plan §3.3: data is stored only when the mode needs it, and only on data messages.
        if (hasData())
        {
            if (mode == DataMode::Full) data.assign(size_t(n) * w, 0);
            else if (mode == DataMode::Stamp) stamp.assign(size_t(n) * stampsPerBeat(), 0);
        }
        deriveMask();
    }

    void Msg::deriveMask()
    {
        unsigned n = beats();
        uint64_t nbytes = bytes();
        if (nbytes >= w)
        {
            // §4.5: the operation uses every lane; a dataless message larger than the bus
            // drives all mask bits HIGH and stays one beat.
            for (auto &m : mask) m = 1;
            return;
        }
        // §4.5: a power-of-two slice of the lanes at address % w (aligned, so it fits).
        for (auto &m : mask) m = 0;
        size_t off = size_t(address % w);
        for (unsigned k = 0; k < n; k++)
            for (uint64_t b = 0; b < nbytes && off + b < w; b++) mask[size_t(k) * w + off + size_t(b)] = 1;
    }

    static std::string hexOf(const uint8_t *lanes, unsigned w)
    {
        // lane w-1 first, one hex digit per 4 lanes (w < 4: the lanes fill the low bits)
        std::string s = "0x";
        unsigned digits = w >= 4 ? w / 4 : 1;
        for (unsigned d = digits; d-- > 0;)
        {
            unsigned v = 0;
            for (unsigned b = 0; b < 4; b++)
            {
                unsigned lane = d * 4 + b;
                if (lane < w && lanes[lane]) v |= 1u << b;
            }
            s += "0123456789abcdef"[v];
        }
        return s;
    }
    std::string Msg::maskHex(unsigned k) const
    {
        if (k >= beats()) return "0x0";
        return hexOf(beatMask(k), w);
    }

    std::string Msg::dataHex(unsigned k) const
    {
        if (!hasData() || data_mode == DataMode::None || k >= beats()) return "-";
        std::string s = "0x";
        char buf[20];
        if (data_mode == DataMode::Full)
        {
            const uint8_t *d = beatData(k);
            for (unsigned lane = w; lane-- > 0;) { std::snprintf(buf, sizeof buf, "%02x", d[lane]); s += buf; }
        }
        else
        {
            unsigned per = stampsPerBeat();
            for (unsigned i = per; i-- > 0;)
            {
                std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)stamp[size_t(k) * per + i]);
                s += buf;
            }
        }
        return s;
    }

    std::string Msg::describe() const
    {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s %s param=%u size=%u src=%llu sink=%llu addr=0x%llx (%u beats)",
                      channelName(channel), opcodeName(channel, opcode), unsigned(param), unsigned(size),
                      (unsigned long long)source, (unsigned long long)sink, (unsigned long long)address, beats());
        return buf;
    }
}}
