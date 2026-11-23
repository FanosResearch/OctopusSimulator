/*
 * File  :      Partitioning.h
 *
 * Partitioning framework (docs/LLC_Banking_and_Partitioning_Design.md s6). A scheme answers
 * WHAT is split and WHERE it is enforced (ways, colours, banks, ...); an ownership answers WHO
 * owns which of the scheme's units. The two are composed, so any ownership works with any scheme.
 * A cache holds its schemes in a PartitionSet. Schemes and ownerships are selected by name from
 * a registry, as replacement policies are.
 */

#ifndef _Partitioning_H
#define _Partitioning_H

#include "AddressMap.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace octopus
{
    // Who owns which units. `core` < 0 is an access with no known requester: it owns everything.
    class Ownership
    {
    public:
        virtual ~Ownership() = default;
        virtual void configure(const std::string &owners, int units) = 0;
        virtual bool owns(int core, int unit) const = 0;
        virtual bool restricts() const = 0;                    // some core is limited
        virtual void onAccess(int core, int unit, bool hit) {}
        virtual void tick(uint64_t cycle) {}
    };

    // What the cache tells a scheme about itself when the scheme is built.
    struct PartitionContext
    {
        std::string cache;            // the cache's configuration name, for messages
        uint32_t ways = 1;
        uint64_t sets = 1;
        const AddressMap *map = nullptr;
    };

    // What is split, and where it is enforced. Every hook has a no-op default.
    class PartitionScheme
    {
    public:
        virtual ~PartitionScheme() = default;
        virtual const char *name() const = 0;
        virtual int units() const = 0;
        void setOwnership(std::unique_ptr<Ownership> o) { m_owners = std::move(o); ownershipChanged(); }
        const Ownership &owners() const { return *m_owners; }

        // cache-side enforcement
        virtual void restrictVictims(int core, uint64_t set, uint32_t &way_mask) const {}
        virtual bool admit(int core, uint64_t addr) const { return true; }

        // source-side enforcement (addresses chosen outside the cache)
        virtual bool     placesAddresses() const { return false; }
        virtual int      unitOf(uint64_t addr) const { return 0; }
        virtual uint64_t withUnit(uint64_t addr, int unit) const { return addr; }

        // observation
        virtual void onAccess(int core, uint64_t addr, bool hit) {}
        virtual void onFill(int core, uint64_t addr) {}
        virtual void tick(uint64_t cycle) {}
        virtual void report(std::ostream &) const {}

    protected:
        virtual void ownershipChanged() {}
        std::unique_ptr<Ownership> m_owners;
    };

    // The schemes of one cache. Forwards every hook to all of them: way masks and admit answers
    // are ANDed. With no schemes every query returns at once.
    class PartitionSet
    {
    public:
        bool empty() const { return m_schemes.empty(); }
        void add(std::unique_ptr<PartitionScheme> s) { m_schemes.push_back(std::move(s)); }
        const std::vector<std::unique_ptr<PartitionScheme>> &schemes() const { return m_schemes; }

        // the ways `core` may fill in `set`, out of `all`
        uint32_t allowedWays(int core, uint64_t set, uint32_t all) const
        {
            if (m_schemes.empty() || core < 0) return all;
            uint32_t m = all;
            for (auto &s : m_schemes) s->restrictVictims(core, set, m);
            return m;
        }
        bool admit(int core, uint64_t addr) const
        {
            for (auto &s : m_schemes) if (!s->admit(core, addr)) return false;
            return true;
        }
        void report(std::ostream &os) const { for (auto &s : m_schemes) s->report(os); }

    private:
        std::vector<std::unique_ptr<PartitionScheme>> m_schemes;
    };

    // Name -> factory, filled by the REGISTER_* macros at static-initialisation time.
    class PartitionRegistry
    {
    public:
        using SchemeFactory = std::function<std::unique_ptr<PartitionScheme>(const PartitionContext &)>;
        using OwnershipFactory = std::function<std::unique_ptr<Ownership>()>;
        static std::map<std::string, SchemeFactory> &schemes() { static std::map<std::string, SchemeFactory> m; return m; }
        static std::map<std::string, OwnershipFactory> &ownerships() { static std::map<std::string, OwnershipFactory> m; return m; }

        // Builds scheme `scheme` with ownership `ownership` configured from `owners`; prints an
        // error naming the known keys and exits on an unknown name.
        static std::unique_ptr<PartitionScheme> build(const std::string &scheme, const std::string &ownership,
                                                      const std::string &owners, const PartitionContext &ctx);
    };

    struct PartitionSchemeRegistrar
    {
        PartitionSchemeRegistrar(const char *key, PartitionRegistry::SchemeFactory f) { PartitionRegistry::schemes()[key] = std::move(f); }
    };
    struct OwnershipRegistrar
    {
        OwnershipRegistrar(const char *key, PartitionRegistry::OwnershipFactory f) { PartitionRegistry::ownerships()[key] = std::move(f); }
    };
}

#define REGISTER_PARTITION_SCHEME(key, T) \
    static octopus::PartitionSchemeRegistrar s_partition_scheme_##T(key, \
        [](const octopus::PartitionContext &c) -> std::unique_ptr<octopus::PartitionScheme> { return std::unique_ptr<octopus::PartitionScheme>(new T(c)); })
#define REGISTER_OWNERSHIP(key, T) \
    static octopus::OwnershipRegistrar s_ownership_##T(key, \
        []() -> std::unique_ptr<octopus::Ownership> { return std::unique_ptr<octopus::Ownership>(new T()); })

#endif /* _Partitioning_H */
