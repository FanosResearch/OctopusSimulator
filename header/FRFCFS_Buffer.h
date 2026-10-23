/*
 * File  :      FRFCFS_Buffer.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On July 8, 2021
 */

#ifndef _FRFCFS_BUFFER_H
#define _FRFCFS_BUFFER_H

#include <vector>
#include <functional>
#include <string>
#include <cstdint>

namespace octopus
{
    enum FRFCFS_State
    {
        Ready = 0,
        NonReady,
        NeedsAction,
        Waiting
    };

    template <class TItem, class TCallback>
    class FRFCFS_Buffer
    {
    private:
        typedef FRFCFS_State (TCallback::*Callback_t)(const TItem &, FRFCFS_State);

        struct Element
        {
            TItem item;
            FRFCFS_State state;
        };

        std::vector<Element> m_buffer;

        TCallback *m_callback_owner;
        Callback_t m_check_state_callback; //called to determine the state of the elements

        int m_max_size; //maximum size of the buffer, if it is -1 the buffer will be unbounded

        // Boundedness note: m_max_size bounds only NEW work admitted as NonReady
        // via pushBack (e.g., demand requests from a lower interface). Several
        // paths deliberately bypass the bound and are always admitted:
        //   * pushFront  -- used for responses/higher-priority messages. A full
        //                   buffer of stalled NonReady requests is drained only
        //                   by the responses that complete them; rejecting a
        //                   response here would deadlock the owner. Responses
        //                   are instead bounded upstream (e.g., by the MSHR
        //                   depth, which limits concurrent outstanding misses).
        //   * pushBack(Ready) -- an already-serviceable item; treated like a
        //                   response for admission.
        //   * pushBack(..., force=true) -- self-generated maintenance traffic
        //                   that MUST be admitted to make forward progress
        //                   (e.g., write-back requests that drain the PWB).
        //                   Bounded upstream by the PWB depth.
        // Do not "tighten" these to honor m_max_size without an explicit
        // reservation, or the owning controller can deadlock.

        // Per-cache-line FCFS ordering. When non-zero, a message may not be
        // serviced ahead of an OLDER pending message to the same cache line
        // (addr & m_line_mask). This enforces point-to-point / per-address
        // order at the controller: if the oldest message to a line can't
        // proceed, no younger message to that line may either. Messages to
        // other lines still flow on readiness. Zero => disabled (legacy FR-FCFS).
        uint64_t m_line_mask;

        // Rescan-skip optimization. Only valid when item readiness is a pure
        // function of state that this buffer's own push/pop mutate -- i.e. it
        // NEVER changes just because cycles elapse. The coherence controller
        // queues qualify (getRequestState = !isStall(cache_line.state, event)),
        // so once a full scan finds nothing ready, nothing can become ready
        // until the next push or successful pop. m_dirty tracks that: a fruitless
        // scan clears it; any push or returned item sets it; a clear m_dirty lets
        // getFirstReady return immediately without re-scanning. Do NOT enable for
        // queues with time-dependent readiness (e.g. MainMemoryController, whose
        // items ripen as m_clk_cycle passes) -- they would stall forever.
        bool m_readiness_state_only;
        bool m_dirty;
        size_t m_last_pop_index = 0;   // where getFirstReady took its last Ready item (restoreLast)

    public:
        FRFCFS_Buffer(Callback_t check_state_callback, TCallback *callback_owner, int max_size = -1,
                      uint64_t line_mask = 0, bool readiness_state_only = false)
        {
            this->m_callback_owner = callback_owner;
            this->m_check_state_callback = check_state_callback;
            this->m_max_size = max_size;
            this->m_line_mask = line_mask;
            this->m_readiness_state_only = readiness_state_only;
            this->m_dirty = true;
        }

        bool pushBack(const TItem &item, FRFCFS_State state = FRFCFS_State::Ready, bool force = false)
        {
            if (!force &&
                state != FRFCFS_State::Ready &&
                this->m_max_size != -1 &&
                (int)this->m_buffer.size() >= this->m_max_size)
                return false;

            Element element = {.state = state};
            element.item = item;

            this->m_buffer.push_back(element);
            this->m_dirty = true;   // new work may be ready or may unblock others
            return true;
        }

        // Interconnect-side intake. Ahead of everything queued from the other
        // side (a bus message keeps the priority upstream's pushFront gave it
        // over the core's own requests) but BEHIND every older message from
        // the same side, so the interconnect's delivery order is preserved
        // while a message waits in the queue. Upstream processed every snoop
        // the cycle it arrived, so their order was never at stake; the address
        // interlock can hold a snoop for a few cycles, and a younger snoop to
        // the same line pushed in front of it made the owner answer the wrong
        // requester (the older GetM then found the line in S, sent no data,
        // and its requester waited forever).
        bool pushFrontOrdered(const TItem &item)
        {
            Element element = {.state = FRFCFS_State::Ready};
            element.item = item;
            int pos = 0;
            for (int i = (int)m_buffer.size() - 1; i >= 0; i--)
                if (m_buffer[i].item.source == item.source)
                {
                    pos = i + 1;
                    break;
                }
            this->m_buffer.insert(this->m_buffer.begin() + pos, element);
            this->m_dirty = true;
            return true;
        }

        bool pushFront(const TItem &item)
        {
            Element element = {.state = FRFCFS_State::Ready};
            element.item = item;
            this->m_buffer.insert(this->m_buffer.begin(), element);
            this->m_dirty = true;   // new work may be ready or may unblock others
            return true;
        }

        // Optional hold: an item for which this returns true is not ready,
        // whatever its state, and keeps its place in the queue (younger demand
        // requests to its line stay behind it through the per-line gate). The
        // cache controller uses it as an address interlock: a line with a
        // deferred data-array access waiting or in flight is busy. The owner
        // must mark the buffer dirty when the hold can have lifted.
        std::function<bool(const TItem &)> m_hold;
        void setHold(std::function<bool(const TItem &)> hold) { m_hold = std::move(hold); }
        // Optional: which items the per-line FCFS gate orders besides invalidations. Unset = the
        // demand requests (TItem::isDemandRequest), as always; a protocol that also orders a CPU Store
        // carrying its bytes sets it (CoherenceProtocolHandler::isCpuDemand).
        std::function<bool(const TItem &)> m_ordered;
        void setOrdered(std::function<bool(const TItem &)> ordered) { m_ordered = std::move(ordered); }
        bool ordered(const TItem &x) const { return (m_ordered ? m_ordered(x) : x.isDemandRequest()) || x.isInvalidation(); }
        // Put an item ahead of every queued ordered item to its line (at the back when there is none),
        // bypassing the bound: work the owner accepted earlier and must not lose (a replayed access,
        // which then precedes the younger accesses to its line through the gate).
        void insertBeforeLine(const TItem &item, FRFCFS_State state)
        {
            size_t at = m_buffer.size();
            for (size_t i = 0; i < m_buffer.size(); i++)
                if (ordered(m_buffer[i].item) && ((m_buffer[i].item.addr ^ item.addr) & m_line_mask) == 0) { at = i; break; }
            m_buffer.insert(m_buffer.begin() + at, Element{item, state});
            m_dirty = true;
        }
        // Debug access to the queued items in order (state dumps).
        const TItem &itemAt(int i) const { return m_buffer[i].item; }
        FRFCFS_State stateAt(int i) const { return m_buffer[i].state; }

        bool getFirstReady(TItem *out_item)
        {
            // Nothing has changed since the last scan came up empty: no queued
            // item can have newly become ready (state-only readiness). Skip the
            // O(n^2) rescan entirely. See m_dirty note above.
            if (m_readiness_state_only && !m_dirty)
                return false;

            if (m_buffer.empty())
            {
                m_dirty = false;
                return false;
            }

            for (int i = 0; i < (int)m_buffer.size(); i++)
            {
                // Address interlock (see m_hold): the item waits in place. It is
                // still an older demand request / invalidation for the gate below.
                if (m_hold && m_hold(m_buffer[i].item))
                    continue;

                // Per-line FCFS gate: a demand request (CPU Load/Store, or GetS/GetM
                // from an L1) OR a back-invalidation may not overtake an OLDER demand
                // request / invalidation to the same cache line. Ordering invalidations
                // too stops an eviction's Own_Invalidation from leapfrogging earlier-
                // broadcast GetS/GetM and orphaning them. DATA responses stay exempt so
                // they can still advance waiting transients (ordering those deadlocks).
                if (this->m_line_mask != 0 && ordered(m_buffer[i].item))
                {
                    bool blocked_by_older_same_line = false;
                    for (int j = 0; j < i; j++)
                    {
                        if (ordered(m_buffer[j].item) &&
                            ((m_buffer[j].item.addr ^ m_buffer[i].item.addr) & this->m_line_mask) == 0)
                        {
                            blocked_by_older_same_line = true;
                            break;
                        }
                    }
                    if (blocked_by_older_same_line)
                        continue;
                }

                FRFCFS_State state = (m_buffer[i].state == FRFCFS_State::Ready) ? FRFCFS_State::Ready :
                                     (m_callback_owner->*m_check_state_callback)(m_buffer[i].item, m_buffer[i].state);
                                     
                if (state == FRFCFS_State::Ready)
                {
                    *out_item = m_buffer[i].item;
                    m_buffer.erase(m_buffer.begin() + i);
                    m_last_pop_index = i;
                    return true;
                }
                else if (state == FRFCFS_State::NeedsAction)
                {
                    *out_item = m_buffer[i].item;
                    m_buffer[i].state = FRFCFS_State::Waiting;
                    return true;
                }
                else
                    m_buffer[i].state = state;
            }
            // Full scan, nothing ready: until the next push or successful pop
            // mutates state, a re-scan cannot find anything new.
            m_dirty = false;
            return false;
        }

        /* Put the item getFirstReady just returned back where it was, so that it keeps its age:
         * a message refused by admission must not let a younger one to the same line overtake it
         * (a home's forward and its later PutAck to the same cache are processed in arrival order). */
        void restoreLast(const TItem &item, FRFCFS_State state = FRFCFS_State::NonReady)
        {
            size_t at = std::min(m_last_pop_index, m_buffer.size());
            m_buffer.insert(m_buffer.begin() + at, Element{item, state});
            m_dirty = true;
        }

        void setCheckStateCallback(Callback_t callback)
        {
            this->m_check_state_callback = callback;
        }

        int size() const { return (int)m_buffer.size(); }

        // Read-only walk over the queued items (diagnostics: hang dumps).
        template <typename F>
        void forEach(F f) const
        {
            for (const Element &e : m_buffer)
                f(e.item, e.state);
        }

        /* Readiness here is a pure function of cache state, and normally that
         * state only changes through this buffer's own push/pop. A state
         * change made elsewhere (a data-array operation completing) must call
         * this, or a stalled item would never be rescanned. */
        void markDirty() { this->m_dirty = true; }
        int maxSize() const { return this->m_max_size; }

        /* Number of queued items satisfying pred(item). */
        template <typename Pred>
        int countIf(Pred pred) const
        {
            int n = 0;
            for (const Element &e : this->m_buffer)
                if (pred(e.item))
                    n++;
            return n;
        }
    };
}

#endif /* _FRFCFS_BUFFER_H */
