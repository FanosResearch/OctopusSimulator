/*
 * File  :      WidthWidget.h
 * WidthWidget (plan §3.5): bus width conversion between one upstream link of w_up bytes and
 * one downstream link of w_down bytes (both powers of two, same level). It repacks beats only,
 * for every opcode and level: control fields, source and sink pass through untouched, so it is
 * legal on TL-C links. By §4.5 lane l of beat k carries the byte at laneBase + k*w + l, so
 * each byte of the operation has one place in either layout; the widget copies byte by byte.
 *   narrow -> wide : 2^k beats merge into one; mask concatenated, corrupt OR-ed (§4.4: the
 *                    merged beat is corrupt if any of its parts was);
 *   wide -> narrow : one beat splits into 2^k; mask sliced, corrupt copied to every slice.
 * A dataless message is one beat on both sides and its mask is re-derived for the new width
 * (§4.5: all HIGH when larger than the bus, the aligned slice otherwise). Data messages carry
 * their mask through (PutPartialData's holes survive). One cycle per path (plan §3.2); a
 * merged beat is offered the cycle after its last part was accepted. D has no address field
 * (Table 9), so a response smaller than the bus is placed by the address of the A request it
 * answers, remembered by source (§6.4: an A source is unique while outstanding); the D
 * message's own fields are passed through unchanged.
 *
 * Config keys (agent[k].*): depth(i) output stage depth per channel (default 2).
 */
#ifndef _TL_AGENTS_WIDTHWIDGET_H
#define _TL_AGENTS_WIDTHWIDGET_H
#include "tilelink/agents/Pipe.h"
#include <map>

namespace octopus { namespace tl
{
    class WidthWidget : public TopologyAgent
    {
    public:
        WidthWidget(const std::string &name, const Config &cfg);
        void validate() const override;
        void reset(uint64_t reset_cycles) override;
        void offer(uint64_t cycle) override;
        void accept(uint64_t cycle) override;
        void commit(uint64_t cycle) override;
        bool idle() const override;
        ProgressHints hints() const override;

    private:
        // One channel: its stage toward the other link and the message being repacked.
        struct Path
        {
            ChannelState *in = nullptr;
            BeatQueue     out;
            MsgPtr        src, dst;   // the message in progress on `in` and its repacked copy
            unsigned      next = 0;   // the next dst beat to release
            uint64_t      geom = 0;   // the operation's address (on D: the request's)
        };
        void repack(Path &p, const Beat &b);
        void repackBeat(Path &p, const Beat &b);

        unsigned m_depth;
        Path     m_path[NUM_CHANNELS];
        std::map<uint64_t, uint64_t> m_req_addr;   // outstanding A source -> request address
    };
}}
#endif /* _TL_AGENTS_WIDTHWIDGET_H */
