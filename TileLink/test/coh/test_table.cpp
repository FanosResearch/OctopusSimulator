/*
 * File  :      test_table.cpp
 * coh::Table (plan §3.7; docs/TileLink_TLC_Tables.md §1.1): the three tables load with the sizes the
 * doc states, every cell equals the doc's cell list (§2.4, §3.4: "the cell text printed here is
 * byte-for-byte the CSV cell") and re-serializes to itself, the MSI manager differs from the MESI one
 * in exactly the cells of the T6 report, and malformed tables are refused with the row named.
 */
#include "TestMain.h"
#include "tilelink/coh/Table.h"
#include <fstream>
#include <map>
#include <sstream>

using namespace octopus::tl;
using namespace octopus::tl::coh;

namespace
{
    std::string path(const char *n) { return std::string(TL_TABLE_DIR) + "/" + n; }

    struct Counts { int faults = 0, stalls = 0, nonempty = 0; };
    Counts count(const Table &t)
    {
        Counts c;
        int f = t.actionId("Fault"), s = t.actionId("Stall");
        for (int st = 0; st < t.numStates(); st++)
            for (int e = 0; e < t.numEvents(); e++)
            {
                const Cell &x = t.transition(st, e);
                if (!x.empty) c.nonempty++;
                if (t.hasAction(st, e, f)) c.faults++;
                if (t.hasAction(st, e, s)) c.stalls++;
            }
        return c;
    }

    // The doc's cell list: "- (state, event) -> `cell`: ...", L1 in §2.4 and the manager in §3.4.
    std::map<std::pair<std::string, std::string>, std::string> docCells(bool manager)
    {
        std::ifstream f(std::string(TL_DOC_DIR) + "/TileLink_TLC_Tables.md");
        std::map<std::pair<std::string, std::string>, std::string> m;
        std::string line;
        int section = 0;
        while (std::getline(f, line))
        {
            if (line.compare(0, 6, "## 2. ") == 0) section = 2;
            else if (line.compare(0, 6, "## 3. ") == 0) section = 3;
            else if (line.compare(0, 6, "## 4. ") == 0) section = 4;
            if ((manager ? 3 : 2) != section || line.compare(0, 3, "- (") != 0) continue;
            size_t comma = line.find(", "), close = line.find(") -> `"), tick = line.find('`', close + 6);
            if (comma == std::string::npos || close == std::string::npos || tick == std::string::npos) continue;
            m[{line.substr(3, comma - 3), line.substr(comma + 2, close - comma - 2)}] = line.substr(close + 6, tick - close - 6);
        }
        return m;
    }

    void roundTrip(const Table &t, bool manager, size_t expected)
    {
        auto doc = docCells(manager);
        CHECK_EQ(doc.size(), expected);
        size_t matched = 0;
        for (int st = 0; st < t.numStates(); st++)
            for (int e = 0; e < t.numEvents(); e++)
            {
                const Cell &c = t.transition(st, e);
                CHECK_EQ(t.cellString(st, e), c.text);   // the parsed form re-serializes to the CSV text
                auto it = doc.find({t.stateName(st), t.eventName(e)});
                if (it == doc.end()) continue;
                matched++;
                if (it->second != c.text) std::printf("    (%s, %s): csv '%s' doc '%s'\n", t.stateName(st).c_str(), t.eventName(e).c_str(), c.text.c_str(), it->second.c_str());
                CHECK_EQ(it->second, c.text);
            }
        CHECK_EQ(matched, expected);
    }

    bool refused(const std::string &text, const std::string &what)
    {
        std::istringstream in(text);
        try { Table::parse(in, "bad.csv"); }
        catch (ConfigError &e)
        {
            if (std::string(e.what()).find(what) != std::string::npos) return true;
            std::printf("    message was: %s\n", e.what());
            return false;
        }
        return false;
    }

    const char *TINY =
        "EventNum,Event\n0,Load\n1,Store\n,\n"
        "ActionNum,Action\n0,Stall\n1,Hit\n,\n"
        "StateNum,State\n0,I\n1,M\n,\n"
        "State,stable,isDataValid,Load,Store\n"
        "I,1,0,Hit/M,M\n"
        "M,1,1,Hit/,\n";
}

TEST(mesi_l1_sizes_and_doc)
{
    Table t = Table::load(path("TLC_MESI_L1.csv"));
    CHECK_EQ(t.numStates(), 10);
    CHECK_EQ(t.numEvents(), 12);
    Counts c = count(t);
    // TLC_Tables §1.3: 120 cells (120 non-empty, 53 Fault, 23 Stall)
    CHECK_EQ(c.nonempty, 120); CHECK_EQ(c.faults, 53); CHECK_EQ(c.stalls, 23);
    roundTrip(t, false, 120);
    CHECK_EQ(t.stateName(t.transition(t.stateId("IS_a"), t.eventId("GrantData_toT")).next), std::string("E"));
    CHECK(t.stable(t.stateId("S")) && !t.stable(t.stateId("SM_a")));
    CHECK(!t.dataValid(t.stateId("I")) && t.dataValid(t.stateId("IS_a")));
    // a bare next state and a stay cell
    const Cell &ra = t.transition(t.stateId("SI_r"), t.eventId("ReleaseAck"));
    CHECK(ra.actions.empty() && ra.next == t.stateId("I"));
    const Cell &hit = t.transition(t.stateId("S"), t.eventId("Load"));
    CHECK(hit.actions.size() == 1 && hit.next == t.stateId("S"));
}

TEST(mesi_llc_sizes_and_doc)
{
    Table t = Table::load(path("TLC_MESI_LLC.csv"));
    CHECK_EQ(t.numStates(), 26);
    CHECK_EQ(t.numEvents(), 26);
    Counts c = count(t);
    CHECK_EQ(c.nonempty, 676); CHECK_EQ(c.faults, 285); CHECK_EQ(c.stalls, 246);
    roundTrip(t, true, 676);
}

TEST(msi_llc_differs_in_the_reported_cells)
{
    Table mesi = Table::load(path("TLC_MESI_LLC.csv")), msi = Table::load(path("TLC_MSI_LLC.csv"));
    CHECK_EQ(msi.numStates(), mesi.numStates() + 1);   // NB_d, appended last (row order = StateNum)
    CHECK_EQ(msi.numEvents(), mesi.numEvents());
    CHECK_EQ(msi.stateName(26), std::string("NB_d"));
    std::vector<std::string> diffs;
    for (int s = 0; s < mesi.numStates(); s++)
        for (int e = 0; e < mesi.numEvents(); e++)
            if (mesi.transition(s, e).text != msi.transition(s, e).text) diffs.push_back(mesi.stateName(s) + "+" + mesi.eventName(e));
    CHECK_EQ(diffs.size(), size_t(2));
    CHECK(diffs.size() == 2 && diffs[0] == "N+AcquireBlock_NtoB" && diffs[1] == "I+AcquireBlock_NtoB");
    CHECK_EQ(msi.transition(msi.stateId("I"), msi.eventId("AcquireBlock_NtoB")).text, std::string("AddSharer/GrantData_toB/B_g"));
    CHECK_EQ(msi.transition(msi.stateId("N"), msi.eventId("AcquireBlock_NtoB")).text, std::string("Get_out/NB_d"));
    // NB_d = NT_d except the fill grants toB to a new sharer
    int nb = msi.stateId("NB_d"), nt = msi.stateId("NT_d");
    for (int e = 0; e < msi.numEvents(); e++)
    {
        if (msi.eventName(e) == "AccessAckData") CHECK_EQ(msi.transition(nb, e).text, std::string("SaveData/AddSharer/GrantData_toB/B_g"));
        else CHECK_EQ(msi.transition(nb, e).text, msi.transition(nt, e).text);
    }
    CHECK(!msi.stable(nb) && msi.dataValid(nb));
    // MSI never grants toT on an NtoB: no cell reachable from NtoB names GrantData_toT except via NtoT paths
    for (int s = 0; s < msi.numStates(); s++)
        CHECK(!msi.hasAction(s, msi.eventId("AcquireBlock_NtoB"), msi.actionId("GrantData_toT")));
}

TEST(tiny_table_parses)
{
    std::istringstream in(TINY);
    Table t = Table::parse(in, "tiny");
    CHECK_EQ(t.numStates(), 2);
    CHECK_EQ(t.stateName(t.transition(0, 0).next), std::string("M"));
    CHECK_EQ(t.cellString(0, 1), std::string("M"));
    CHECK(t.transition(1, 1).empty && t.transition(1, 1).next == 1);   // a missing trailing cell = stay
    CHECK_EQ(t.eventId("Nope"), -1);
    bool threw = false;
    try { t.requireAction("Nope"); } catch (ConfigError &) { threw = true; }
    CHECK(threw);
}

TEST(malformed_tables_refused)
{
    std::string s = TINY;
    auto rep = [&](const std::string &a, const std::string &b) { std::string x = s; x.replace(x.find(a), a.size(), b); return x; };
    CHECK(refused(rep("I,1,0,Hit/M,M\nM,1,1,Hit/,\n", "M,1,1,Hit/,\nI,1,0,Hit/M,M\n"), "row 0 is state 'M' but StateNum 0 is I"));
    CHECK(refused(rep("I,1,0,Hit/M", "I,1,0,Hot/M"), "row I, event Load, cell 'Hot/M': unknown action Hot"));
    CHECK(refused(rep("I,1,0,Hit/M,M", "I,1,0,Hit/M,X"), "row I, event Store, cell 'X': unknown next state X"));
    CHECK(refused(s + "\n", "a blank line after the last row"));
    CHECK(refused(rep("State,stable,isDataValid,Load,Store", "State,stable,isDataValid,Store,Load"), "event column 0 is Store but EventNum 0 is Load"));
    CHECK(refused(rep("I,1,0,", "I,2,0,"), "row I: stable must be 0 or 1"));
    CHECK(refused(rep("I,1,0,Hit/M", "I,1,0,/M"), "empty action name"));
    CHECK(refused(rep("1,Store", "2,Store"), "event Store has number 2, expected 1"));
    CHECK(refused(rep("M,1,1,Hit/,\n", "M,1,1,Hit/,Hit/,Hit/\n"), "beyond the last event column"));
    CHECK(refused(rep("StateNum,State\n0,I\n1,M\n,\n", ""), "table header before the EventNum, ActionNum and StateNum sections"));
    bool threw = false;
    try { Table::load(path("no_such_table.csv")); } catch (ConfigError &) { threw = true; }
    CHECK(threw);
}

TL_TEST_MAIN("TL-COH-ENGINE table")
