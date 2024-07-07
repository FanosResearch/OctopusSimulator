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

    public:
        FRFCFS_Buffer(Callback_t check_state_callback, TCallback *callback_owner, int max_size = -1)
        {
            this->m_callback_owner = callback_owner;
            this->m_check_state_callback = check_state_callback;
            this->m_max_size = max_size;
        }

        bool pushBack(const TItem &item, FRFCFS_State state = FRFCFS_State::Ready)
        {
            if (state != FRFCFS_State::Ready &&
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
