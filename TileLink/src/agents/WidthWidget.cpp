/*
 * File  :      WidthWidget.cpp
 * WidthWidget (plan §3.5): beat repacking by absolute byte address (§4.5).
 */
#include "tilelink/agents/WidthWidget.h"

namespace octopus { namespace tl
{
    WidthWidget::WidthWidget(const std::string &name, const Config &cfg) : TopologyAgent(name)
    {
        int d = cfg.get<int>("depth", 2);
        if (d <= 0) throw ConfigError("WidthWidget " + name + ": depth must be >= 1");
        m_depth = unsigned(d);
    }

    void WidthWidget::validate() const { requireOneEach("WidthWidget", false); }

    void WidthWidget::reset(uint64_t)
    {
        validate();
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            Channel ch = static_cast<Channel>(c);
            Path &p = m_path[c];
            p = Path();
            m_req_addr.clear();
            if (!up()->has(ch)) continue;
            p.in = &inChannel(ch);
            p.out.out = &outChannel(ch);
            p.out.depth = m_depth;
        }
    }

    void WidthWidget::offer(uint64_t) { for (auto &p : m_path) p.out.offer(); }

    void WidthWidget::accept(uint64_t)
    {
        // Ready from this channel's own stage (§4.1, plan §3.2). A message in the middle of
        // being merged keeps being accepted as long as the stage has room for what it will
        // release; the parts are held in the copy, not in the stage.
        for (auto &p : m_path) if (p.in) p.in->setReady(p.out.space());
    }

    void WidthWidget::repack(Path &p, const Beat &b)
    {
        const Msg &src = *b.msg;
        // §6.4: the source is outstanding up to the cycle its response is received; the
        // request's address is forgotten with the response's last beat.
        repackBeat(p, b);
        if (src.channel == Channel::D && src.opcode != unsigned(OpD::ReleaseAck) && b.last()) m_req_addr.erase(src.source);
    }

    void WidthWidget::repackBeat(Path &p, const Beat &b)
    {
        const Msg &src = *b.msg;
        unsigned w_out = p.out.out->link()->params().w;
        if (b.index == 0)
        {
            p.src = b.msg;
            p.dst = cloneMsg(src, w_out);   // same control fields; mask derived for w_out (§4.5)
            p.next = 0;
            p.geom = src.address;
            if (src.channel == Channel::A) m_req_addr[src.source] = src.address;
            else if (src.channel == Channel::D && src.opcode != unsigned(OpD::ReleaseAck))
            {
                // Table 12: AccessAck*, HintAck, Grant* answer an A request with this source;
                // ReleaseAck answers a C request and carries no data to place.
                auto it = m_req_addr.find(src.source);
                if (it != m_req_addr.end()) p.geom = it->second;
            }
            if (!src.hasData())
            {
                // §4.5: a dataless message is one beat whatever the width; its mask was
                // re-derived by the layout (all HIGH when larger than the bus).
                p.out.push(p.dst, 0);
                p.src = nullptr; p.dst = nullptr;
                return;
            }
        }
        Msg &dst = *p.dst;
        uint64_t first, last, bytes = src.bytes();
        if (beatRange(p.geom, bytes, src.w, b.index, first, last))
        {
            // Every byte of this beat goes to its place in the other layout (§4.5: lane =
            // address modulo width, beat = offset / width). Mask and data travel with it.
            for (uint64_t x = first; x <= last; x++) copyByte(dst, p.geom, src, p.geom, x);
            // §4.4: corrupt is per beat. Merging ORs the parts; splitting copies to every slice.
            if (src.beatCorrupt(b.index))
            {
                unsigned kd0 = lanePos(p.geom, w_out, first).beat, kd1 = lanePos(p.geom, w_out, last).beat;
                for (unsigned k = kd0; k <= kd1; k++) dst.corrupt[k] = 1;
            }
            // Release every output beat whose last byte has now arrived (the input beats
            // come in address order, §4.1).
            while (p.next < dst.beats())
            {
                uint64_t f, l;
                if (!beatRange(p.geom, bytes, w_out, p.next, f, l) || l > last) break;
                p.out.push(p.dst, p.next);
                p.next++;
            }
        }
        if (b.last())
        {
            while (p.next < dst.beats()) p.out.push(p.dst, p.next++);   // defensive: nothing left unreleased
            p.src = nullptr; p.dst = nullptr;
        }
    }

    void WidthWidget::commit(uint64_t)
    {
        for (auto &p : m_path)
        {
            if (!p.in) continue;
            p.out.commit();
            const Beat &b = p.in->accepted();
            if (b.msg) repack(p, b);
        }
    }

    bool WidthWidget::idle() const
    {
        for (auto &p : m_path) if (!p.out.empty() || p.dst) return false;
        return true;
    }

    ProgressHints WidthWidget::hints() const
    {
        // Plan §3.6: a merged beat waits for all its parts: the width ratio, plus one cycle.
        ProgressHints h;
        unsigned a = up()->params().w, b = down()->params().w;
        h.latency = 1 + (a > b ? a / b : b / a);
        h.queue_depth = m_depth;
        return h;
    }
}}
