/*
 * File  :      NetworkOnly.cpp
 */
#include "../../header/SystemConfigurations/NetworkOnly.h"
#include "../../header/Logger.h"
using namespace std;
namespace octopus
{
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
            // transpose: the endpoint at the router with swapped coordinates, if any
            int transpose = -1;
            int r = t.routerOf(id); auto c = t.coord(r);
            if (t.kind() == net::Topology::Kind::Grid2D || t.kind() == net::Topology::Kind::Torus2D)
            {
                int tr = c.first * t.dimX() + c.second;
                if (c.first < t.dimY() && c.second < t.dimX() && tr < t.routers() && !t.endpointsAt(tr).empty())
                    transpose = t.endpointsAt(tr)[0];
            }
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
