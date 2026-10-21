/*
 * File  :      Msg.h
 * One TileLink message: the control fields (constant for the whole burst, §4.1), the per-beat
 * data signals (mask, data, corrupt, §3 Table 3), and the simulator-only fields. Plan §3.3.
 *
 * A channel moves a shared_ptr<Msg> plus a beat index, so a burst is never copied per beat.
 * The Msg knows the bus width w it is laid out for (set by layout()), so beats() needs no
 * argument; beatAddress(k) = address + k*w is derived and is never a wire field (plan §1.4).
 */
#ifndef _TL_MSG_H
#define _TL_MSG_H
#include "Types.h"
#include <memory>
#include <string>
#include <vector>

namespace octopus { namespace tl
{
    // Plan §3.3: what the data signals carry. The Msg stores data only when the mode needs it.
    enum class DataMode : uint8_t { None = 0, Stamp = 1, Full = 2 };
    bool parseDataMode(const std::string &s, DataMode &out);   // "none" | "stamp" | "full"
    const char *dataModeName(DataMode m);

    constexpr uint64_t NEVER = ~uint64_t(0);   // a timestamp not yet taken

    // The control fields (type C in Table 3): identical on every beat of a burst (§4.1).
    struct MsgHeader
    {
        Channel  channel = Channel::A;
        uint8_t  opcode  = 0;
        uint8_t  param   = 0;
        uint8_t  size    = 0;     // log2 of the operation size in bytes (§4.5)
        uint64_t source  = 0;     // a/b/c/d_source (§6.4)
        uint64_t sink    = 0;     // d/e_sink (§6.4)
        uint64_t address = 0;     // the wire address, constant for the burst (§4.1, §8.3)
        bool     denied  = false; // d_denied only (§4.4)
        bool operator==(const MsgHeader &o) const;
        bool operator!=(const MsgHeader &o) const { return !(*this == o); }
    };

    struct Msg : MsgHeader
    {
        // ---- per-beat data signals (type D in Table 3), sized by layout()
        DataMode              data_mode = DataMode::None;
        unsigned              w = 0;          // bus width in bytes the beats are laid out for
        std::vector<uint8_t>  mask;           // beats*w lanes, 1 = active (§4.5); every message
        std::vector<uint8_t>  corrupt;        // one per beat (§4.4); only data messages may set it
        std::vector<uint8_t>  data;           // Full: beats*w bytes, little-endian lane order (§4.5)
        std::vector<uint64_t> stamp;          // Stamp: one 64-bit value per 8 bytes (plan §3.3)

        // ---- simulator-only fields (never on the wire). A Msg object may be forwarded over
        // several links unchanged; the timestamps are then end to end: the first two are set
        // once (on the first link), the last one on every link it completes on. Per-link
        // latency is ChannelState::lastLatency().
        uint64_t id = 0;                      // Kernel::newMsgId(), unique per run
        int      from_agent = -1;             // the emitting agent's id
        uint64_t t_first_offer  = NEVER;      // cycle beat 0 was first offered (valid HIGH)
        uint64_t t_first_accept = NEVER;      // cycle beat 0 was first accepted: in progress from here (§5.1)
        uint64_t t_last_accept  = NEVER;      // cycle the last beat was accepted, on the latest link

        Msg() = default;
        Msg(Channel ch, unsigned opcode, unsigned size, unsigned w, DataMode mode = DataMode::None);
        static std::shared_ptr<Msg> make(Channel ch, unsigned opcode, unsigned size, unsigned w,
                                         DataMode mode = DataMode::None);

        // Sizes mask/corrupt/data/stamp for the current (channel, opcode, size) on a bus of w
        // bytes, and derives the mask per §4.5. Call again after changing size or address.
        void layout(unsigned w, DataMode mode);

        bool     hasData() const { return tl::hasData(channel, opcode); }
        bool     isRequest() const { return tl::isRequest(channel, opcode); }
        uint64_t bytes() const { return uint64_t(1) << size; }
        // §4: max(1, 2^size / w) beats for a data message, one beat otherwise.
        unsigned beats() const { return beatCount(hasData(), size, w); }
        // §4.5: beat k carries the bytes at address + k*w. Derived, never a wire field.
        uint64_t beatAddress(unsigned k) const { return address + uint64_t(k) * w; }
        const MsgHeader &header() const { return *this; }

        // §4.5 mask derivation: LOW on inactive lanes, HIGH on active lanes. Smaller than the
        // bus: the 2^size lanes at address % w. Bus-sized or larger: every lane of every beat
        // HIGH, which for a dataless message means one beat of all-HIGH. PutPartialData may
        // then lower bits by hand.
        void deriveMask();
        uint8_t       *beatMask(unsigned k)       { return &mask[size_t(k) * w]; }
        const uint8_t *beatMask(unsigned k) const { return &mask[size_t(k) * w]; }
        uint8_t       *beatData(unsigned k)       { return data.empty() ? nullptr : &data[size_t(k) * w]; }
        const uint8_t *beatData(unsigned k) const { return data.empty() ? nullptr : &data[size_t(k) * w]; }
        unsigned stampsPerBeat() const { return w >= 8 ? w / 8 : 1; }
        bool     beatCorrupt(unsigned k) const { return k < corrupt.size() && corrupt[k] != 0; }

        // Transcript formatting (plan §4): mask as hex with lane w-1 first, "0x" prefix; data
        // as hex (Full: one byte per lane, lane w-1 first; Stamp: the beat's stamps), or "-"
        // in mode None.
        std::string maskHex(unsigned k) const;
        std::string dataHex(unsigned k) const;
        std::string describe() const;   // "A PutFullData size=5 src=1 addr=0x40 (4 beats)"
    };
    using MsgPtr = std::shared_ptr<Msg>;
}}
#endif /* _TL_MSG_H */
