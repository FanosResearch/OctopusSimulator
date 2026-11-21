/*
 * File  :      Builtin.cpp
 * The topology registry, the key view and the list of shipped builders
 * (docs/NoC_Topology_Plugin_Plan.md s3.4).
 */
#include "../../../../header/Interconnect/Network/TopologyBuilder.h"

namespace octopus { namespace net
{
    // ------------------------------------------------------------------ KeyView
    int KeyView::geti(const std::string &k) const { return std::get<int>(m_map.at(k).value); }
    std::string KeyView::gets(const std::string &k) const { return std::get<std::string>(m_map.at(k).value); }
    std::vector<int> KeyView::getvi(const std::string &k) const { return std::get<std::vector<int>>(m_map.at(k).value); }
    std::vector<std::vector<int>> KeyView::rows(const std::string &key) const
    {
        std::vector<std::vector<int>> out;
        for (int n = 0; has(key + "[" + std::to_string(n) + "]"); n++) out.push_back(getvi(key + "[" + std::to_string(n) + "]"));
        return out;
    }
    std::map<int, int> KeyView::nodeMap(const std::vector<int> &endpoints) const
    {
        std::map<int, int> node_map;
        for (int id : endpoints)
        {
            std::string k = "node_map[" + std::to_string(id) + "]";
            if (!has(k)) throw ConfigError("endpoint " + std::to_string(id) + " has no " + k + " (which router it attaches to)");
            node_map[id] = geti(k);
        }
        return node_map;
    }

    // ------------------------------------------------------------------ registry
    TopologyRegistry &TopologyRegistry::builtin()
    {
        static TopologyRegistry reg;
        static bool filled = false;
        if (!filled) { filled = true; registerBuiltinTopologies(reg); }
        return reg;
    }
    void TopologyRegistry::add(std::unique_ptr<TopologyBuilder> b)
    {
        if (find(b->name())) throw ConfigError("topology builder '" + b->name() + "' registered twice");
        m_builders.push_back(std::move(b));
    }
    const TopologyBuilder *TopologyRegistry::find(const std::string &name) const
    {
        for (auto &b : m_builders) if (b->name() == name) return b.get();
        return nullptr;
    }
    std::vector<std::string> TopologyRegistry::names() const
    {
        std::vector<std::string> n; for (auto &b : m_builders) n.push_back(b->name()); return n;
    }
    Topology TopologyRegistry::create(const std::string &name, const KeyView &cfg, const std::vector<int> &endpoint_ids, int local_ports) const
    {
        const TopologyBuilder *b = find(name);
        if (!b)
        {
            std::string list; for (auto &n : names()) list += (list.empty() ? "" : ", ") + n;
            throw ConfigError("unknown topology '" + name + "' (" + list + ")");
        }
        return b->build(cfg, endpoint_ids, local_ports);
    }

    // ------------------------------------------------------------------ the shipped builders
    std::unique_ptr<TopologyBuilder> makeCustomBuilder();
    std::unique_ptr<TopologyBuilder> makeGrid2DBuilder();
    std::unique_ptr<TopologyBuilder> makeTorus2DBuilder();
    std::unique_ptr<TopologyBuilder> makeRingBuilder();
    std::unique_ptr<TopologyBuilder> makeStarBuilder();

    void registerBuiltinTopologies(TopologyRegistry &reg)
    {
        reg.add(makeCustomBuilder());
        reg.add(makeGrid2DBuilder());
        reg.add(makeTorus2DBuilder());
        reg.add(makeRingBuilder());
        reg.add(makeStarBuilder());
    }
}}
