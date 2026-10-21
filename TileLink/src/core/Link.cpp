/*
 * File  :      Link.cpp
 */
#include "tilelink/Link.h"
#include "tilelink/Agent.h"

namespace octopus { namespace tl
{
    Link::Link(const std::string &name, const LinkParams &params, Agent *master, Agent *slave)
        : m_name(name), m_params(params), m_master(master), m_slave(slave)
    {
        // Plan §1.1: A+D at TL-UL/UH, A..E at TL-C.
        for (int c = 0; c < NUM_CHANNELS; c++)
        {
            Channel ch = static_cast<Channel>(c);
            if (m_params.hasChannel(ch)) m_ch[c].reset(new ChannelState(this, ch));
        }
    }

    int Link::masterId() const { return m_master ? m_master->id() : -1; }
    int Link::slaveId() const { return m_slave ? m_slave->id() : -1; }

    ChannelState &Link::chan(Channel ch)
    {
        auto &p = m_ch[static_cast<int>(ch)];
        if (!p) fatal("link " + m_name + " (" + m_params.describe() + ") has no channel " + channelName(ch));
        return *p;
    }
    const ChannelState &Link::chan(Channel ch) const { return const_cast<Link *>(this)->chan(ch); }
}}
