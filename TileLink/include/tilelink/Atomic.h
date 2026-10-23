/*
 * File  :      Atomic.h
 * The atomic operations of §8.2.1 (ArithmeticData) and §8.2.2 (LogicalData) on the whole 2^size-byte
 * operand, as little-endian byte vectors (docs/TileLink_Review_Action_Plan.md problem 4): ADD carries
 * through every byte and drops only the final carry; MIN / MAX compare as signed integers (the sign is the
 * top byte's high bit), MINU / MAXU as unsigned, both from the highest byte down; XOR / OR / AND / SWAP
 * are per byte. Pure: no beats, lanes or agent state. The one definition the executing agents (RamSlave,
 * CoherentManager) and the checker (ValueCheck) share, so the model and the execution cannot drift.
 */
#ifndef _TL_ATOMIC_H
#define _TL_ATOMIC_H
#include "tilelink/Types.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace octopus { namespace tl
{
    // a < b, n-byte little-endian integers (signed: two's complement)
    inline bool atomicLess(const uint8_t *a, const uint8_t *b, size_t n, bool is_signed)
    {
        if (n == 0) return false;
        if (is_signed)
        {
            bool sa = (a[n - 1] & 0x80) != 0, sb = (b[n - 1] & 0x80) != 0;
            if (sa != sb) return sa;   // the negative one is smaller
        }
        for (size_t i = n; i-- > 0;)
            if (a[i] != b[i]) return a[i] < b[i];
        return false;
    }

    // out = op(old, operand), n bytes each; an unknown opcode/param leaves the old value
    inline void atomicOp(unsigned opcode, unsigned param, const uint8_t *old, const uint8_t *opnd, uint8_t *out, size_t n)
    {
        if (opcode == unsigned(OpA::ArithmeticData))
        {
            const uint8_t *pick = old;
            switch (param)
            {
                case unsigned(Arith::ADD):
                {
                    unsigned carry = 0;
                    for (size_t i = 0; i < n; i++) { unsigned s = unsigned(old[i]) + opnd[i] + carry; out[i] = uint8_t(s); carry = s >> 8; }
                    return;
                }
                case unsigned(Arith::MIN):  pick = atomicLess(opnd, old, n, true)  ? opnd : old; break;
                case unsigned(Arith::MAX):  pick = atomicLess(old, opnd, n, true)  ? opnd : old; break;
                case unsigned(Arith::MINU): pick = atomicLess(opnd, old, n, false) ? opnd : old; break;
                case unsigned(Arith::MAXU): pick = atomicLess(old, opnd, n, false) ? opnd : old; break;
            }
            for (size_t i = 0; i < n; i++) out[i] = pick[i];
            return;
        }
        for (size_t i = 0; i < n; i++)
            switch (param)
            {
                case unsigned(Logic::XOR):  out[i] = uint8_t(old[i] ^ opnd[i]); break;
                case unsigned(Logic::OR):   out[i] = uint8_t(old[i] | opnd[i]); break;
                case unsigned(Logic::AND):  out[i] = uint8_t(old[i] & opnd[i]); break;
                case unsigned(Logic::SWAP): out[i] = opnd[i]; break;
                default: out[i] = old[i]; break;
            }
    }

    // the same on vectors of equal non-zero length (an empty result on a length mismatch)
    inline std::vector<uint8_t> applyAtomic(unsigned opcode, unsigned param, const std::vector<uint8_t> &old, const std::vector<uint8_t> &opnd)
    {
        if (old.empty() || old.size() != opnd.size()) return {};
        std::vector<uint8_t> out(old.size());
        atomicOp(opcode, param, old.data(), opnd.data(), out.data(), old.size());
        return out;
    }
}}
#endif /* _TL_ATOMIC_H */
