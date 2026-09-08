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

        bool pushFront(const TItem &item)
        {
            Element element = {.state = FRFCFS_State::Ready};
            element.item = item;
            this->m_buffer.insert(this->m_buffer.begin(), element);
            this->m_dirty = true;   // new work may be ready or may unblock others
            return true;
        }

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
                // Per-line FCFS gate: a demand request (CPU Load/Store, or GetS/GetM
                // from an L1) OR a back-invalidation may not overtake an OLDER demand
                // request / invalidation to the same cache line. Ordering invalidations
                // too stops an eviction's Own_Invalidation from leapfrogging earlier-
                // broadcast GetS/GetM and orphaning them. DATA responses stay exempt so
                // they can still advance waiting transients (ordering those deadlocks).
                if (this->m_line_mask != 0 &&
                    (m_buffer[i].item.isDemandRequest() || m_buffer[i].item.isInvalidation()))
                {
                    bool blocked_by_older_same_line = false;
                    for (int j = 0; j < i; j++)
                    {
                        if ((m_buffer[j].item.isDemandRequest() || m_buffer[j].item.isInvalidation()) &&
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

        void setCheckStateCallback(Callback_t callback)
        {
            this->m_check_state_callback = callback;
        }
    };
}

#endif /* _FRFCFS_BUFFER_H */
