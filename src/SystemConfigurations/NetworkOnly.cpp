/*
 * File  :      NetworkOnly.cpp
 */
#include "../../header/SystemConfigurations/NetworkOnly.h"
#include "../../header/Logger.h"
#include <algorithm>
#include <random>
using namespace std;
namespace octopus
{
    // The partner of endpoint `id` under a permutation pattern; -1 if none or itself. Bit patterns
    // act on the endpoint's index among the sorted ids (a power of two of them); transpose and
    // tornado act on the coordinates of the endpoint's router on a lattice.
    static int partner(const std::string &pattern, int id, const vector<int> &ids, const net::Topology &t, const Configurable::ParametersMap &gp)
    {
        int N = (int)ids.size(), i = (int)(std::find(ids.begin(), ids.end(), id) - ids.begin());
        auto atRouter = [&](int r) { return (r >= 0 && r < t.routers() && !t.endpointsAt(r).empty()) ? t.endpointsAt(r)[0] : -1; };
        int out = -1;
        if (pattern == "transpose")
        {
            // the endpoint at the router with swapped coordinates, if any (2-D lattices)
            int r = t.routerOf(id);
            const net::Lattice *L = t.lattice();
            if (L && L->rank() == 2)
            {
                auto c = L->coord(r);
                int tr = c[0] * L->dims[0] + c[1];
                if (c[0] < L->dims[1] && c[1] < L->dims[0] && tr < t.routers() && !t.endpointsAt(tr).empty())
                    out = t.endpointsAt(tr)[0];
            }
            return out;                                     // transpose keeps -1 = its fallback
        }
        if (pattern == "bitcomp" || pattern == "bitrev" || pattern == "shuffle")
        {
            int b = 0; while ((1 << b) < N) b++;
            if ((1 << b) != N) throw net::ConfigError("generator pattern " + pattern + " needs a power-of-two number of endpoints, got " + std::to_string(N));
            int j = 0;
            if (pattern == "bitcomp") j = ~i & (N - 1);
            else if (pattern == "bitrev") { for (int k = 0; k < b; k++) if (i & (1 << k)) j |= 1 << (b - 1 - k); }
            else j = b == 0 ? 0 : (((i << 1) | (i >> (b - 1))) & (N - 1));
            out = ids[j];
        }
        else if (pattern == "tornado")
        {
            // each lattice dimension: c' = (c + ceil(k/2) - 1) mod k; on a non-lattice, the ring of indices
            const net::Lattice *L = t.lattice();
            if (L)
            {
                auto c = L->coord(t.routerOf(id));
                for (int d = 0; d < L->rank(); d++) { int k = L->dims[d]; c[d] = (c[d] + (k + 1) / 2 - 1) % k; }
                out = atRouter(L->index(c));
            }
            else out = ids[(i + (N + 1) / 2 - 1) % N];
        }
        else if (pattern == "randperm")
        {
            // one permutation for all generators: Fisher-Yates over mt19937 (portable, unlike std::shuffle)
            int seed = gp.count("perm_seed") ? std::get<int>(gp.at("perm_seed").value) : 1;   // TrafficGenerator.csv's default
            std::mt19937 rng((uint32_t)seed);
            vector<int> perm(N); for (int k = 0; k < N; k++) perm[k] = k;
            for (int k = N - 1; k > 0; k--) { int j = (int)(rng() % (uint32_t)(k + 1)); std::swap(perm[k], perm[j]); }
            out = ids[perm[i]];
        }
        return out == id ? -1 : out;
    }

    NetworkOnly::NetworkOnly(vector<string> cl_params, string config_path, string config_name)
        : Configurable(cl_params, config_path, config_name)
    {
        string name = STRINGIFY(NetworkOnly);
        m_net = new net::Network(getSubMap(STRINGIFY(interconnect)), name);
        const net::Topology &t = m_net->topology();
        vector<int> ids = t.endpoints();
        std::sort(ids.begin(), ids.end());
        for (int id : ids)
        {
            vector<int> others; for (int o : ids) if (o != id) others.push_back(o);
            // the destination of a permutation pattern (docs/NoC_Architecture_Spec.md s18)
            ParametersMap gp = getSubMap(STRINGIFY(generator), id);
            std::string pattern = gp.count("pattern") ? std::get<std::string>(gp.at("pattern").value) : std::string("uniform");
            int transpose = net::TrafficGenerator::isPermutation(pattern) ? partner(pattern, id, ids, t, gp) : -1;
            m_gens.push_back(new net::TrafficGenerator(getSubMap(STRINGIFY(generator), id), id, m_net->getInterfaceFor(id), others, transpose, name));
        }
        m_net->standalone(m_gens);
        Logger::getLogger()->registerReportPath("newLogger");
    }
    NetworkOnly::~NetworkOnly()
    {
        delete m_net;                       // first: its destructor writes NetworkStats.csv, which reads the generators
        for (auto g : m_gens) delete g;
    }
}
