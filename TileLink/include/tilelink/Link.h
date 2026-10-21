/*
 * File  :      Link.h
 * A link: master to slave, its parameters and level, and the channels it has (A+D for
 * TL-UL/UH, A..E for TL-C). Plan §1.1, §3.4.
 */
#ifndef _TL_LINK_H
#define _TL_LINK_H
#include "Channel.h"
#include "Params.h"
#include <array>
#include <memory>
#include <string>

namespace octopus { namespace tl
{
    class Agent;

    class Link
    {
    public:
        Link(const std::string &name, const LinkParams &params, Agent *master, Agent *slave);

        const std::string &name() const { return m_name; }
        const LinkParams  &params() const { return m_params; }
        Level              level() const { return m_params.level; }
        Agent             *master() const { return m_master; }
        Agent             *slave() const { return m_slave; }
        int                masterId() const;
        int                slaveId() const;

        bool          has(Channel ch) const { return m_ch[static_cast<int>(ch)] != nullptr; }
        ChannelState &chan(Channel ch);                   // fatal if the link has no such channel
        const ChannelState &chan(Channel ch) const;
        // §4: the sending and receiving agents of a channel.
        Agent *sender(Channel ch) const { return masterSends(ch) ? m_master : m_slave; }
        Agent *receiver(Channel ch) const { return masterSends(ch) ? m_slave : m_master; }

        // Every channel the link has, in priority order.
        template <typename F> void forEachChannel(F f)
        {
            for (auto &c : m_ch) if (c) f(*c);
        }

    private:
        std::string m_name;
        LinkParams  m_params;
        Agent      *m_master, *m_slave;
        std::array<std::unique_ptr<ChannelState>, NUM_CHANNELS> m_ch;
    };
}}
#endif /* _TL_LINK_H */
