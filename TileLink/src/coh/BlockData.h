/*
 * File  :      BlockData.h
 * Internal to the TL-C agents (CacheMaster, CoherentManager): one cached block's data in the
 * kernel's data mode (plan §3.3: full = the bytes, stamp = one 64-bit value per 8-byte group,
 * none = nothing), and the copies between a block and the lanes of a message (§4.5: lane l of
 * beat k carries the byte at laneBase + k*w + l), whatever the bus width.
 */
#ifndef _TL_COH_BLOCKDATA_H
#define _TL_COH_BLOCKDATA_H
#include "tilelink/agents/Pipe.h"
#include <cstdint>
#include <vector>

namespace octopus { namespace tl { namespace coh
{
    struct BlockData
    {
        DataMode              mode = DataMode::None;
        uint64_t              base = 0;      // the block address
        unsigned              bytes_n = 0;   // block size
        std::vector<uint8_t>  bytes;         // Full
        std::vector<uint64_t> stamps;        // Stamp: one per 8 bytes (a block below 8 bytes: one)

        void init(DataMode m, uint64_t block, unsigned block_bytes)
        {
            mode = m; base = block; bytes_n = block_bytes;
            bytes.assign(m == DataMode::Full ? block_bytes : 0, 0);
            stamps.assign(m == DataMode::Stamp ? (block_bytes + 7) / 8 : 0, 0);
        }
        size_t group(uint64_t x) const { return size_t((x - base) / 8); }

        // The value of byte x as the checkers see it (Full: the byte; Stamp: its group's stamp).
        uint64_t value(uint64_t x) const
        {
            if (mode == DataMode::Full) return bytes[size_t(x - base)];
            if (mode == DataMode::Stamp) return stamps[group(x)];
            return 0;
        }
        void set(uint64_t x, uint64_t v)
        {
            if (mode == DataMode::Full) bytes[size_t(x - base)] = uint8_t(v);
            else if (mode == DataMode::Stamp) stamps[group(x)] = v;
        }

        // Bytes [first, last] of the block from a message laid out at geometry address `geom`
        // (the wire address; on D the request's). masked_only: only the lanes whose mask bit is HIGH
        // (PutPartialData, §4.5); A and B carry a mask, C and D do not (every lane counts).
        void fromMsg(const Msg &m, uint64_t geom, uint64_t first, uint64_t last, bool masked_only)
        {
            if (mode == DataMode::None) return;
            for (uint64_t x = first; x <= last; x++)
            {
                if (masked_only)
                {
                    LanePos p = lanePos(geom, m.w, x);
                    size_t i = size_t(p.beat) * m.w + p.lane;
                    if (i < m.mask.size() && !m.mask[i]) continue;
                }
                set(x, laneValue(m, geom, x));
            }
        }
        // Bytes [first, last] of the block into the lanes of a message at geometry address `geom`.
        void toMsg(Msg &m, uint64_t geom, uint64_t first, uint64_t last) const
        {
            if (mode == DataMode::None) return;
            for (uint64_t x = first; x <= last; x++)
            {
                LanePos p = lanePos(geom, m.w, x);
                if (mode == DataMode::Full)
                {
                    size_t i = size_t(p.beat) * m.w + p.lane;
                    if (i < m.data.size()) m.data[i] = bytes[size_t(x - base)];
                }
                else
                {
                    size_t i = size_t(p.beat) * m.stampsPerBeat() + (m.w >= 8 ? p.lane / 8 : 0);
                    if (i < m.stamp.size()) m.stamp[i] = stamps[group(x)];
                }
            }
        }
        void wholeFromMsg(const Msg &m) { fromMsg(m, base, base, base + bytes_n - 1, false); }
        void wholeToMsg(Msg &m) const { toMsg(m, base, base, base + bytes_n - 1); }

        // The bytes a checker compares for [addr, addr+n): Full: the bytes themselves; Stamp: the
        // little-endian bytes of each covered 8-byte group's stamp (the whole group: a stamp
        // write replaces the group, plan §3.3). Returns the start address of what was written.
        uint64_t checkBytes(uint64_t addr, unsigned n, std::vector<uint8_t> &out) const
        {
            out.clear();
            if (mode == DataMode::Full)
            {
                for (unsigned i = 0; i < n; i++) out.push_back(bytes[size_t(addr + i - base)]);
                return addr;
            }
            uint64_t g0 = addr & ~uint64_t(7), g1 = (addr + n + 7) & ~uint64_t(7);
            for (uint64_t g = g0; g < g1; g += 8)
            {
                uint64_t s = stamps[group(g)];
                for (unsigned b = 0; b < 8; b++) out.push_back(uint8_t(s >> (8 * b)));
            }
            return g0;
        }
    };
}}}
#endif /* _TL_COH_BLOCKDATA_H */
