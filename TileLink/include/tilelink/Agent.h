/*
 * File  :      Agent.h
 * The abstract agent (§5.1: a participant that possesses one or more links), its three phase
 * hooks (plan §3.2), its declared capability per link (plan §3.4), and the agent registry the
 * driver builds systems from (plan §4). NullMaster/NullSlave are the trivial pair the tests
 * and tlsim use before real agents exist (T2).
 */
#ifndef _TL_AGENT_H
#define _TL_AGENT_H
#include "Config.h"
#include "Link.h"
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace octopus { namespace tl
{
    class Kernel;

    enum class Role : uint8_t { Master = 0, Slave = 1 };
    const char *roleName(Role r);

    struct LinkRole
    {
        Link *link;
        Role  role;
    };

    class Agent
    {
    public:
        explicit Agent(const std::string &name) : m_name(name) {}
        virtual ~Agent() {}

        const std::string &name() const { return m_name; }
        int  id() const { return m_id; }           // index in the kernel's evaluation order
        Kernel *kernel() const { return m_kernel; }
        const std::vector<LinkRole> &links() const { return m_links; }

        // Plan §3.4: the conformance level this agent supports on that link in that role.
        // Elaboration refuses a link whose level is above it.
        virtual Level capability(const Link &link, Role role) const = 0;

        // Plan §3.2, the three phases. An offer in cycle t must be computed from the agent's
        // state at the start of t (what was committed in t-1 at the latest): no combinational
        // pass-through, and valid never depends on ready (§4.1).
        virtual void offer(uint64_t cycle) = 0;    // phase 1: raise valid on the channels it sends on
        virtual void accept(uint64_t cycle) = 0;   // phase 2: set ready on the channels it receives on
        virtual void commit(uint64_t cycle) = 0;   // phase 3: consume accepted beats, update state

        // Called once by the kernel before cycle 0 with the reset length (§3.2.2: valid must be
        // LOW while reset is asserted; Kernel::inReset() tells the agent when that is).
        virtual void reset(uint64_t reset_cycles) { (void)reset_cycles; }
        // runUntilIdle stops when no channel is valid and every agent reports idle.
        virtual bool idle() const { return true; }

        // Plan §3.2: all randomness comes from one seeded RNG per agent.
        std::mt19937_64 &rng() { return m_rng; }

        // Kernel wiring (called by Kernel::add / Kernel::connect).
        void attachKernel(Kernel *k, int id, uint64_t seed);
        void attachLink(Link *l, Role r) { m_links.push_back({l, r}); }

    protected:
        std::string m_name;
        int m_id = -1;
        Kernel *m_kernel = nullptr;
        std::vector<LinkRole> m_links;
        std::mt19937_64 m_rng;
    };

    // Does nothing: never raises valid. Capability TL-C so it fits on any link.
    class NullMaster : public Agent
    {
    public:
        explicit NullMaster(const std::string &name) : Agent(name) {}
        Level capability(const Link &, Role) const override { return Level::C; }
        void offer(uint64_t) override {}
        void accept(uint64_t) override {}
        void commit(uint64_t) override {}
    };
    // Does nothing: never raises ready. (A slave that accepted and never answered would break
    // rule 3ii; one that never accepts is simply not ready, legal while nothing is presented.)
    class NullSlave : public Agent
    {
    public:
        explicit NullSlave(const std::string &name) : Agent(name) {}
        Level capability(const Link &, Role) const override { return Level::C; }
        void offer(uint64_t) override {}
        void accept(uint64_t) override {}
        void commit(uint64_t) override {}
    };

    // Plan §4: agent types by name, for `agent[k].type(s)`. The factory receives the agent's
    // name and its own parameter sub-map (`agent[k].*`).
    class AgentRegistry
    {
    public:
        using Factory = std::function<std::unique_ptr<Agent>(const std::string &name, const Config &params)>;
        void add(const std::string &type, Factory f) { m_types[type] = std::move(f); }
        bool has(const std::string &type) const { return m_types.count(type) > 0; }
        std::unique_ptr<Agent> make(const std::string &type, const std::string &name, const Config &params) const;
        std::vector<std::string> types() const;
        // NullMaster and NullSlave registered.
        static AgentRegistry withBuiltins();

    private:
        std::map<std::string, Factory> m_types;
    };
}}
#endif /* _TL_AGENT_H */
