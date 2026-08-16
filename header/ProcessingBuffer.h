/*
 * File  :      ProcessingBuffer.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On July 8, 2021
 */

#ifndef _PROCESSING_BUFFER_H
#define _PROCESSING_BUFFER_H

#include <vector>
#include <string>
#include <functional>
#include "Arbiter.h"

namespace ns3
{
    enum EntryState
    {
        Ready = 0,
        NonReady,
        NeedsAction,
        Waiting
    };

    template <class TItem>
    class ProcessingBuffer
    {
    private:
        typedef std::function<EntryState(const TItem &, EntryState)> Callback;

        struct Element
        {
            TItem item;
            EntryState state;
        };

        std::vector<Element> m_buffer;
        Callback m_check_state_callback; //called to determine the state of the elements

        int m_max_size; //maximum size of the buffer, if it is -1 the buffer will be unbounded
        Arbiter *m_arbiter;

        bool getFirstReady(TItem *out_item)
        {
            if (m_buffer.empty())
                return false;

            for (int i = 0; i < (int)m_buffer.size(); i++)
            {
                EntryState state = (m_buffer[i].state == EntryState::Ready) ? EntryState::Ready : 
                                     m_check_state_callback(m_buffer[i].item, m_buffer[i].state);
                                     
                if (state == EntryState::Ready)
                {
                    *out_item = m_buffer[i].item;
                    m_buffer.erase(m_buffer.begin() + i);
                    return true;
                }
                else if (state == EntryState::NeedsAction)
                {
                    *out_item = m_buffer[i].item;
                    m_buffer[i].state = EntryState::Waiting;
                    return true;
                }
                else
                    m_buffer[i].state = state;
            }
            return false;
        }

    public:
        ProcessingBuffer(Callback check_state_callback, int max_size = -1, Arbiter* arbiter = NULL) : 
            m_check_state_callback(check_state_callback), 
            m_max_size(max_size), 
            m_arbiter(arbiter)
        {
        }
        ~ProcessingBuffer()
        {
            if(m_arbiter)
                delete m_arbiter;
        }

        Arbiter* getArbiter()
        {
            return m_arbiter;
        }

        bool pushBack(const TItem &item, EntryState state = EntryState::Ready)
        {
            if (state != EntryState::Ready &&
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
            Element element = {.state = EntryState::Ready};
            element.item = item;
            this->m_buffer.insert(this->m_buffer.begin(), element);
            return true;
        }

        bool getReady(TItem *out_item)
        {
            if(m_arbiter != NULL)
            {
                if (m_buffer.empty())
                return false;
                
                vector<vector<Message>*> arbiter_buf;
                arbiter_buf.push_back(new vector<Message>());

                for (int i = 0; i < (int)m_buffer.size(); i++)
                {
                    EntryState state = (m_buffer[i].state == EntryState::Ready) ? EntryState::Ready : 
                                        m_check_state_callback(m_buffer[i].item, m_buffer[i].state);
                                        
                    if (state == EntryState::Ready)
                        arbiter_buf[0]->push_back(m_buffer[i].item);
                }
                if(arbiter_buf[0]->empty())
                {
                    delete arbiter_buf[0];
                    return false;
                }
                bool msg_found = m_arbiter->elect(0, arbiter_buf, out_item);
                if(msg_found)
                {
                    auto iter = std::find_if(m_buffer.begin(), m_buffer.end(), [&] (Element elem) -> bool {
                                    return elem.item == *out_item;
                                });
                    m_buffer.erase(iter);
                }

                delete arbiter_buf[0];
                return msg_found;
            }
            else
                return getFirstReady(out_item);
        }

        void setCheckStateCallback(Callback callback)
        {
            this->m_check_state_callback = callback;
        }

        bool empty()
        {
            return m_buffer.empty();
        }

        bool find(TItem &item, TItem *out_item, std::function<bool(TItem &, TItem &)> check_fn)
        {
            for(Element elem : m_buffer)
            {
                if(check_fn(item, elem.item))
                {
                    *out_item = elem.item;
                    return true;
                }
            }
            return false;
        }

        bool removeItem(TItem &item)
        {
            auto iter = find_if(m_buffer.begin(), m_buffer.end(), 
                [&] (Element elem) -> bool{
                    return elem.item == item;
                });
            
            if(iter != m_buffer.end())
            {
                m_buffer.erase(iter);
                return true;
            }
            return false;
        }
    };
}

#endif /* _PROCESSING_BUFFER_H */
