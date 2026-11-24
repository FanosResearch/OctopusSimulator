/*
 * File  :      CacheDataHandler.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On April 27, 2022
 */
#include "../header/CacheDataHandler.h"
#include "../header/Logger.h"
#include "PagePlacer.h"
#include "DramMap.h"

#include <algorithm>
#include <fstream>
#include <set>

namespace octopus
{
    CacheDataHandler::CacheDataHandler(ParametersMap map, string pname, string config_path, string name) : 
        Configurable(map, config_path, name, pname)
    {
        //Parameters initialization
        int cache_size = std::get<int>(parameters.at(STRINGIFY(cache_size)).value);
        m_block_size = std::get<int>(parameters.at(STRINGIFY(m_block_size)).value);
        m_ways_count = std::get<int>(parameters.at(STRINGIFY(m_ways_count)).value);
        m_data_access_latency = std::get<int>(parameters.at(STRINGIFY(m_data_access_latency)).value);
        // Optional, so presets written before these existed keep the occupancy model.
        m_data_array_pipelined = false;
        if (parameters.find(STRINGIFY(m_data_array_pipelined)) != parameters.end())
            m_data_array_pipelined = std::get<int>(parameters.at(STRINGIFY(m_data_array_pipelined)).value) != 0;
        m_data_array_ports = 1;
        if (parameters.find(STRINGIFY(m_data_array_ports)) != parameters.end())
            m_data_array_ports = std::get<int>(parameters.at(STRINGIFY(m_data_array_ports)).value);
        if (m_data_array_ports == 0)
            m_data_array_ports = 1;

        string replacement_policy_name = std::get<string>(parameters.at(STRINGIFY(replacement_policy_name)).value);

        //Constructor
        int lines_count = cache_size / m_block_size;
        m_sets_count = lines_count / m_ways_count;
        m_map.setGeometry(m_block_size, m_sets_count);

        // Data-array sub-banks (design doc s5.4): optional, default one = the whole array.
        int sub_banks = 1, sub_bank_shift = 0;
        if (parameters.find(STRINGIFY(sub_banks)) != parameters.end())
            sub_banks = std::get<int>(parameters.at(STRINGIFY(sub_banks)).value);
        if (parameters.find(STRINGIFY(sub_bank_shift)) != parameters.end())
            sub_bank_shift = std::get<int>(parameters.at(STRINGIFY(sub_bank_shift)).value);
        if (sub_banks < 1 || sub_bank_shift < 0 || (sub_banks > 1 && m_data_array_pipelined))
        {
            cerr << name << ": sub_banks(i) must be >= 1 and sub_bank_shift(i) >= 0; sub-banks model the occupancy"
                 << " data array, so they cannot be combined with m_data_array_pipelined (use m_data_array_ports)" << endl;
            exit(2);
        }
        m_map.setSubBanks(sub_banks, sub_bank_shift);
        m_sub_ready.assign(sub_banks, 0);

        m_cache = new GenericCacheLine[lines_count];
        m_replacement_policy = Policy::getReplacementPolicy(replacement_policy_name, m_ways_count);

        m_all_mask = (m_ways_count >= 32) ? 0xffffffffu : ((1u << m_ways_count) - 1);
        handlers().push_back(this);

        dprint = new DebugPrint(getSubMap(STRINGIFY(dprint)), name, parent_name + "." + name);

        m_cycle = 0;
    }

    // partition[k].scheme(s) / ownership(s) (default static) / owners(s), k = 0, 1, ...; the
    // way_partition(s) "<spec>" alias is partition[0] = way / static / <spec>.
    void CacheDataHandler::buildPartitions()
    {
        PartitionContext ctx;
        ctx.cache = parent_name + "." + name;
        if (m_map.banked()) ctx.cache += "[bank " + std::to_string(m_map.bankIndex()) + "]";
        m_partitions.setName(ctx.cache);
        ctx.ways = m_ways_count; ctx.sets = m_sets_count; ctx.map = &m_map;
        ctx.mshr_entries = m_mshr_entries; ctx.pwb_entries = pwbEntries();
        auto str = [](const ParametersMap &m, const char *key, const std::string &dflt) {
            auto it = m.find(key);
            return it == m.end() ? dflt : std::get<std::string>(it->second.value);
        };
        std::string alias = str(parameters, STRINGIFY(way_partition), "");
        if (!getSubMap("partition", 1 << 20).empty())
        {
            cerr << ctx.cache << ": partition[*] is not supported; number the partitions partition[0], partition[1], ..." << endl;
            exit(2);
        }
        if (!alias.empty())
        {
            if (!getSubMap("partition", 0).empty())
            {
                cerr << ctx.cache << ": way_partition(s) is an alias for partition[0]; set one or the other" << endl;
                exit(2);
            }
            m_partitions.add(PartitionRegistry::build("way", "static", alias, ctx));   // never strict
        }
        for (int k = alias.empty() ? 0 : 1; ; k++)
        {
            ParametersMap pm = getSubMap("partition", k);
            if (pm.empty()) break;
            std::string scheme = str(pm, "scheme", "");
            if (scheme.empty())
            {
                cerr << ctx.cache << ": partition[" << k << "] has no scheme(s)" << endl;
                exit(2);
            }
            bool strict = pm.count("strict") && std::get<int>(pm.at("strict").value) != 0;
            PartitionContext c = ctx;
            if (pm.count("page_size")) c.page_bytes = (uint64_t)std::get<int>(pm.at("page_size").value);
            c.pwb_owner = str(pm, "pwb_owner", "requester");
            m_partitions.add(PartitionRegistry::build(scheme, str(pm, "ownership", "static"), str(pm, "owners", ""), c), strict);
        }
        if (m_partitions.empty()) return;
        // Partitions.csv in the report directory: fills and violations per cache, scheme, core.
        // Written once for all partitioned handlers (the first one registers the hook).
        static bool hooked = false;   // a handler replaced by a derived one leaves the list, not the hook
        if (!hooked && (hooked = true))
            Logger::atRunEnd([](const std::string &dir) {
                std::ofstream f(dir + "/Partitions.csv");
                f << "Cache,Partition,Scheme,Core,Fills,Violations,SharedOutside\n";
                uint64_t v = 0;
                for (CacheDataHandler *h : handlers()) { h->m_partitions.report(f); v += h->m_partitions.violations(); }
                DramMap::instance().partitions().report(f); v += DramMap::instance().partitions().violations();
                cout << "Partitioning: " << v << " violation(s); per-core fills in " << dir << "/Partitions.csv" << endl;
                for (CacheDataHandler *h : handlers()) h->m_partitions.summary(cout);
                if (PagePlacer::instance().enabled()) PagePlacer::instance().report(cout);
            });
    }

    std::vector<CacheDataHandler *> &CacheDataHandler::handlers()
    {
        static std::vector<CacheDataHandler *> v;
        return v;
    }

    void CacheDataHandler::finalizePartitions(bool page_placer, int num_cores)
    {
        std::vector<const PartitionScheme *> schemes;
        DramMap &dram = DramMap::instance();
        dram.clearPartitions();
        std::set<std::string> dram_seen;   // one DRAM scheme per signature (every LLC bank has a copy)
        for (CacheDataHandler *h : handlers())
        {
            h->ensurePartitions();
            const auto &own = h->m_partitions.schemes();
            for (size_t k = 0; k < own.size(); k++)
            {
                schemes.push_back(own[k].get());
                if (own[k]->countedAtMemory() && dram_seen.insert(own[k]->signature()).second)
                    dram.partitions().watch(own[k].get(), (int)k, h->m_partitions.strict(k));
            }
        }
        PagePlacer::instance().configure(page_placer, schemes, num_cores);
    }

    CacheDataHandler::~CacheDataHandler()
    {
        auto &v = handlers();
        v.erase(std::remove(v.begin(), v.end(), this), v.end());
        delete[] m_cache;
        delete m_replacement_policy;
    }

    void CacheDataHandler::initializeCacheStates(int initialState)
    {
        for (uint32_t set = 0; set < m_sets_count; set++)
        {
            for(uint32_t way = 0; way < m_ways_count; way++)
            {
                ((GenericCacheLine *)getLine(set, way))->state = initialState;
                ((GenericCacheLine *)getLine(set, way))->valid = false;
                ((GenericCacheLine *)getLine(set, way))->m_block_size = this->m_block_size;
            }
        }
    }

    void CacheDataHandler::initializeCacheLine(GenericCacheLine *line)
    {
        *line = GenericCacheLine();
        line->m_block_size = this->m_block_size;
        line->m_data = new uint8_t[line->m_block_size];
        
        memset(line->m_data, 0, line->m_block_size);
    }
 
    uint32_t CacheDataHandler::getBlockSize()
    {
        return m_block_size;
    }

    uint32_t CacheDataHandler::getDataAccessLatency()
    {
        return m_data_access_latency;
    }

    bool CacheDataHandler::findline(uint64_t address, uint64_t *set, int *way)
    {
        *set = calculate_set(address);

        for (uint32_t index = 0; index < m_ways_count; index++)
        {
            GenericCacheLine *cache_line = (GenericCacheLine *)getLine(*set, index);

            if (cache_line->valid == true && cache_line->tag == (int64_t)calculate_tag(address))
            {
                *way = index;
                return true;
            }
        }
        return false;
    }

    bool CacheDataHandler::writeCacheLine_bypassLatency(uint64_t address, GenericCacheLine *line, bool soft_write, int core)
    {
        uint64_t set = calculate_set(address);
        int way = findEmptyWay(address, core);
        if (way == -1)
            return false;

        *((GenericCacheLine *)getLine(set, way)) = *line;
        ((GenericCacheLine *)getLine(set, way))->tag = calculate_tag(address);
        ((GenericCacheLine *)getLine(set, way))->placed_by = core;
        if (core >= 0 && !m_partitions.empty()) m_partitions.onFill(core, address, set, way);   // built by findEmptyWay
        
        if(!soft_write)
            m_replacement_policy->update(set, way, m_cycle); //ToDo: this should change to support allocation on miss

        return true;
    }

    bool CacheDataHandler::writeCacheLine(uint64_t address, GenericCacheLine *line, int core)
    {
        if (!isReady(address))
            return false;

        if(line->m_data != NULL)
            occupy(address);
        
        return writeCacheLine_bypassLatency(address, line, false, core);
    }

    bool CacheDataHandler::updateLineBits(uint64_t address, GenericCacheLine *line)
    {
        uint64_t set;
        int way;
        if (findline(address, &set, &way))
        {
            ((GenericCacheLine *)getLine(set, way))->copyBits(*line);
            ((GenericCacheLine *)getLine(set, way))->m_block_size = this->m_block_size;
            return true;
        }
        else
            return false;
    }

    bool CacheDataHandler::updateLineData(uint64_t address, const uint8_t *data, int core)
    {
        uint64_t set;
        int way;
        if (findline(address, &set, &way) && isReady(address))
        {
            ((GenericCacheLine *)getLine(set, way))->copyData(data);
            if (core >= 0) ((GenericCacheLine *)getLine(set, way))->placed_by = core;
            occupy(address);
            m_replacement_policy->update(set, way, m_cycle); //ToDo: this should change to support allocation on miss
            return true;
        }
        else
            return false;
    }

    bool CacheDataHandler::modifyData(uint64_t address, const uint8_t *data, uint16_t size, bool soft_write)
    {
        uint64_t set;
        int way;
        if (findline(address, &set, &way) && (isReady(address) || soft_write))
        {
            uint16_t offset = address & (m_block_size - 1);
            ((GenericCacheLine *)getLine(set, way))->modifyData(data, offset, size);

            occupy(address);
            if(!soft_write)
                m_replacement_policy->update(set, way, m_cycle); //ToDo: this should change to support allocation on miss
            return true;
        }
        else
            return false;
    }

    bool CacheDataHandler::readCacheLine(uint64_t address, GenericCacheLine *out_line, bool soft_read)
    {
        uint64_t set;
        int way;
        if (findline(address, &set, &way) && isReady(address))
        {
            if (out_line != NULL)
            {    
                *out_line = *((GenericCacheLine *)getLine(set, way));
                occupy(address);
            }
            if(!soft_read)
                m_replacement_policy->update(set, way, m_cycle); //ToDo: this should change to support allocation on miss
            return true;
        }
        else
            return false;
    }

    bool CacheDataHandler::readLineBits(uint64_t address, GenericCacheLine *out_line)
    {
        uint64_t set;
        int way;
        if (findline(address, &set, &way))
        {
            if (out_line != NULL)
                out_line->copyBits(*((GenericCacheLine *)getLine(set, way)));
            return true;
        }
        else
            return false;
    }

    int CacheDataHandler::findEmptyWay(uint64_t address, int core)
    {
        ensurePartitions();
        uint64_t set = calculate_set(address);

        uint32_t allowed = allowedWays(core, set);
        for (uint32_t way = 0; way < m_ways_count; way++)
        {
            if (!(allowed & (1u << way))) continue;
            GenericCacheLine *cache_line = (GenericCacheLine *)getLine(set, way);

            if (cache_line->valid == false)
            {
                return way;
            }
        }
        return -1;
    }

    uint64_t CacheDataHandler::getEvictionCandidate(uint64_t address, GenericCacheLine *line)
    {
        // if (m_replacement_policy == ReplcPolicy::RANDOM)
        // {
        //     uint64_t set = calculate_set(address);
        //     uint64_t way = rand() % m_ways_count;
            
        //     line->copyBits(*((GenericCacheLine *)getLine(set, way)));
        //     return calculate_address(((GenericCacheLine *)getLine(set, way))->tag, set);
        // }
        return 0;
    }

    void CacheDataHandler::updateCycle(uint64_t cycle)
    {
        m_cycle = cycle;
    }

    bool CacheDataHandler::isReady()
    {
        // Pipelined: the array never closes; the controller paces accesses.
        if (m_data_array_pipelined)
            return true;
        for (uint64_t r : m_sub_ready)
            if (r <= m_cycle)
                return true;
        return false;
    }

    bool CacheDataHandler::isReady(uint64_t address)
    {
        return portFree(address);
    }

    CacheDataHandler::LineLocation CacheDataHandler::lineLocation(uint64_t address)
    {
        uint64_t set;
        int way;
        return CacheDataHandler::findline(address, &set, &way) ? LineLocation::ARRAY : LineLocation::NONE;
    }
}