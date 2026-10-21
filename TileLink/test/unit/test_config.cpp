/*
 * File  :      test_config.cpp
 * TL-UNIT: the Octopus CSV parameter format round trip, sub-maps, wildcards, overrides,
 * Extends, and Kernel::build from a config.
 */
#include "TestMain.h"
#include "tilelink/Config.h"
#include "tilelink/Kernel.h"
#include <cstdio>
#include <fstream>

using namespace octopus::tl;

static std::string writeTemp(const std::string &name, const std::string &text)
{
    std::string path = "tl_cfg_" + name;
    std::ofstream f(path);
    f << text;
    return path;
}

TEST(octopus_syntax)
{
    // Lines in the exact shape of MultiCoreSystem_Mesh.csv: trailing commas, # comments,
    // vectors ending in a comment cell, blank and comma-only lines, dotted sub-maps.
    std::string path = writeTemp("syntax.csv",
        "workload_path(s),BMs/TestBM/,,,#Absolute path is recommended,,\n"
        "num_cores(i),4,,,,,\n"
        "ratio(d),0.5,,\n"
        ",,,,,,\n"
        "   \n"
        "# a full comment line\n"
        "bus.m_lower_level_ids(vi),10,,,#LLC ID,,\n"
        "bus.interconnect_controller.candidates_id(vi),0,1,2,3,10,#Candidates are the l1 ids and llc id\n"
        "cache_controller[*].protocol_type(s),DIRECTORY_MSI,,,,,\n"
        "cache_controller[*].processing_queue_size(i),16,,#L1 depth,,,\n"
        "cache_controller[0].m_id(i),0,,,,,\n"
        "cache_controller[1].m_id(i),1,,,,,\n"
        "cache_controller[1].protocol_type(s),OTHER,,,,,\n"
        "llc_controller.dprint.cond_addr(vs),,,,,,#Acceptable Addresses\n"
        "names(vs),alpha,beta,,gamma,#done\n"
        "hex(i),0x10,,\n"
        "empty_s(s),,,\n");
    Config c;
    std::string err;
    CHECK(c.load(path, &err));
    CHECK_EQ(err, std::string(""));
    CHECK_EQ(c.get<std::string>("workload_path"), std::string("BMs/TestBM/"));
    CHECK_EQ(c.get<int>("num_cores"), 4);
    CHECK_EQ(c.get<double>("ratio"), 0.5);
    CHECK_EQ(c.get<int>("hex"), 16);
    CHECK_EQ(c.get<std::string>("empty_s"), std::string(""));
    CHECK(!c.has("missing"));
    CHECK_EQ(c.get<int>("missing", 42), 42);
    std::vector<std::string> names = c.get<std::vector<std::string>>("names");
    CHECK_EQ(names.size(), size_t(3));
    if (names.size() == 3) { CHECK_EQ(names[0], std::string("alpha")); CHECK_EQ(names[2], std::string("gamma")); }
    // sub-maps: the text before the first dot
    Config bus = c.sub("bus");
    std::vector<int> ids = bus.get<std::vector<int>>("m_lower_level_ids");
    CHECK_EQ(ids.size(), size_t(1)); if (!ids.empty()) CHECK_EQ(ids[0], 10);
    std::vector<int> cand = bus.get<std::vector<int>>("interconnect_controller.candidates_id");
    CHECK_EQ(cand.size(), size_t(5)); if (cand.size() == 5) { CHECK_EQ(cand[0], 0); CHECK_EQ(cand[4], 10); }
    std::vector<std::string> empty = c.sub("llc_controller").get<std::vector<std::string>>("dprint.cond_addr");
    CHECK(empty.empty());
    // instances and wildcards: the instance's entries win
    std::vector<int> inst = c.instances("cache_controller");
    CHECK_EQ(inst.size(), size_t(2)); if (inst.size() == 2) { CHECK_EQ(inst[0], 0); CHECK_EQ(inst[1], 1); }
    CHECK(c.instances("bus").empty());
    Config cc0 = c.sub("cache_controller", 0), cc1 = c.sub("cache_controller", 1), cc5 = c.sub("cache_controller", 5);
    CHECK_EQ(cc0.get<std::string>("protocol_type"), std::string("DIRECTORY_MSI"));
    CHECK_EQ(cc0.get<int>("processing_queue_size"), 16);
    CHECK_EQ(cc0.get<int>("m_id"), 0);
    CHECK_EQ(cc1.get<std::string>("protocol_type"), std::string("OTHER"));
    CHECK_EQ(cc1.get<int>("m_id"), 1);
    CHECK_EQ(cc5.get<std::string>("protocol_type"), std::string("DIRECTORY_MSI"));   // wildcard only
    CHECK(!cc5.has("m_id"));
    CHECK(c.hasSub("cache_controller", 7)); CHECK(!c.hasSub("nothing", 1)); CHECK(c.hasSub("bus"));
    // type errors
    bool threw = false;
    try { c.get<int>("workload_path"); } catch (ConfigError &) { threw = true; }
    CHECK(threw);
    threw = false;
    try { c.get<int>("missing"); } catch (ConfigError &) { threw = true; }
    CHECK(threw);
    std::remove(path.c_str());
}

TEST(overrides_and_errors)
{
    Config c;
    c.set("num_cores(i)=8");
    c.set("name(s)=hello world");
    c.set("agent[2].type(s)=NullMaster");
    c.set("list(vi)=1,2,3");
    c.set("plain(s),with,comma");       // file syntax also accepted
    CHECK_EQ(c.get<int>("num_cores"), 8);
    CHECK_EQ(c.get<std::string>("name"), std::string("hello world"));
    CHECK_EQ(c.sub("agent", 2).get<std::string>("type"), std::string("NullMaster"));
    CHECK_EQ(c.get<std::vector<int>>("list").size(), size_t(3));
    CHECK_EQ(c.get<std::string>("plain"), std::string("with"));
    c.set("num_cores(i)=2");            // later entries replace earlier ones
    CHECK_EQ(c.get<int>("num_cores"), 2);
    auto bad = [&](const char *line) { bool t = false; try { c.set(line); } catch (ConfigError &) { t = true; } return t; };
    CHECK(bad("noparens,3"));
    CHECK(bad("x(q),3"));
    CHECK(bad("x(i),notanumber"));
    CHECK(bad("x(vi),1,two"));
    CHECK(bad("x(i)3"));
    std::string err;
    CHECK(!c.load("/nonexistent/tl.csv", &err));
    CHECK(err.find("cannot open") != std::string::npos);
    std::string path = writeTemp("bad.csv", "ok(i),1\nbroken line\n");
    CHECK(!c.load(path, &err));
    CHECK(err.find(":2:") != std::string::npos);
    std::remove(path.c_str());
}

TEST(extends)
{
    std::string base = writeTemp("base.csv", "w(i),8\nlevel(s),UL\nagent[0].type(s),NullMaster\n");
    std::string top = writeTemp("top.csv", "Extends,tl_cfg_base\nlevel(s),UH\nextra(i),1\n");
    Config c;
    std::string err;
    CHECK(c.load(top, &err));
    CHECK_EQ(err, std::string(""));
    CHECK_EQ(c.get<int>("w"), 8);
    CHECK_EQ(c.get<std::string>("level"), std::string("UH"));   // the extending file wins
    CHECK_EQ(c.get<int>("extra"), 1);
    CHECK_EQ(c.sub("agent", 0).get<std::string>("type"), std::string("NullMaster"));
    std::remove(base.c_str()); std::remove(top.c_str());
}

TEST(build_from_config)
{
    Config c;
    c.set("reset_cycles(i)=5");
    c.set("seed(i)=99");
    c.set("data_mode(s)=stamp");
    c.set("agent[0].type(s)=NullMaster");
    c.set("agent[0].name(s)=cpu");
    c.set("agent[1].type(s)=NullSlave");
    c.set("agent[2].type(s)=NullSlave");
    c.set("agent[2].name(s)=mem");
    c.set("link[*].w(i)=16");
    c.set("link[*].level(s)=C");
    c.set("link[0].master(s)=cpu");
    c.set("link[0].slave(s)=agent1");
    c.set("link[0].o(i)=3");
    c.set("link[1].name(s)=outer");
    c.set("link[1].master(s)=cpu");
    c.set("link[1].slave(s)=mem");
    c.set("link[1].level(s)=UH");
    c.set("link[1].w(i)=4");
    Kernel k;
    k.build(c, AgentRegistry::withBuiltins());
    CHECK_EQ(k.options().reset_cycles, uint64_t(5));
    CHECK_EQ(k.options().seed, uint64_t(99));
    CHECK_EQ(int(k.options().data_mode), int(DataMode::Stamp));
    CHECK_EQ(k.agents().size(), size_t(3));
    CHECK_EQ(k.links().size(), size_t(2));
    CHECK(k.agent("cpu") != nullptr); CHECK(k.agent("agent1") != nullptr); CHECK(k.agent("mem") != nullptr);
    Link *l0 = k.link("link0"), *l1 = k.link("outer");
    CHECK(l0 != nullptr); CHECK(l1 != nullptr);
    if (l0)
    {
        CHECK_EQ(l0->params().w, 16u); CHECK_EQ(l0->params().o, 3u); CHECK_EQ(int(l0->level()), int(Level::C));
        CHECK(l0->master() == k.agent("cpu")); CHECK(l0->slave() == k.agent("agent1"));
        CHECK(l0->has(Channel::E));
    }
    if (l1)
    {
        CHECK_EQ(l1->params().w, 4u); CHECK_EQ(int(l1->level()), int(Level::UH));   // instance wins over wildcard
        CHECK(!l1->has(Channel::B));
    }
    CHECK_EQ(k.agent("cpu")->links().size(), size_t(2));
    CHECK_EQ(k.agent("mem")->links().size(), size_t(1));
    CHECK_EQ(int(k.agent("mem")->links()[0].role), int(Role::Slave));
    k.run(12);
    CHECK_EQ(k.cycle(), uint64_t(12));
    // registry
    AgentRegistry reg = AgentRegistry::withBuiltins();
    CHECK(reg.has("NullMaster")); CHECK(reg.has("NullSlave")); CHECK(!reg.has("FuzzMaster"));
    bool threw = false;
    try { reg.make("FuzzMaster", "x", Config()); } catch (ConfigError &e) { threw = std::string(e.what()).find("unknown agent type") != std::string::npos; }
    CHECK(threw);
}

TL_TEST_MAIN("test_config")
