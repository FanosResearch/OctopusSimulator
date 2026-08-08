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

    public:
        FRFCFS_Buffer(Callback_t check_state_callback, TCallback *callback_owner, int max_size = -1,
                      uint64_t line_mask = 0)
        {
            this->m_callback_owner = callback_owner;
            this->m_check_state_callback = check_state_callback;
            this->m_max_size = max_size;
            this->m_line_mask = line_mask;
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
            return true;
        }

        bool pushFront(const TItem &item)
        {
            Element element = {.state = FRFCFS_State::Ready};
            element.item = item;
            this->m_buffer.insert(this->m_buffer.begin(), element);
            return true;
        }

        bool getFirstReady(TItem *out_item)
        {
            if (m_buffer.empty())
                return false;

            for (int i = 0; i < (int)m_buffer.size(); i++)
            {
                // Per-line FCFS gate: a demand request (CPU Load/Store, or GetS/GetM
                // from an L1) may not overtake an OLDER demand request to the same cache
                // line. Only demand requests are ordered -- data responses, writebacks,
                // back-invalidations and snoops are service traffic and must stay free to
                // advance transients (e.g. an eviction's Own_Invalidation), else the
                // controller deadlocks. An older demand request to the line blocks a
                // younger one regardless of the younger one's kind.
                if (this->m_line_mask != 0 && m_buffer[i].item.isDemandRequest())
                {
                    bool blocked_by_older_same_line = false;
                    for (int j = 0; j < i; j++)
                    {
                        if (m_buffer[j].item.isDemandRequest() &&
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
            return false;
        }

        void setCheckStateCallback(Callback_t callback)
        {
            this->m_check_state_callback = callback;
        }
    };
}

#endif /* _FRFCFS_BUFFER_H */
