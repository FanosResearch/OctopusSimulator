/*
 * File  :      Table.cpp
 * The FSM CSV reader of the policy engine (docs/TileLink_Plan.md §3.7; the format is
 * docs/TileLink_TLC_Tables.md §1.1, as Octopus's src/FSMReader.cpp reads it).
 */
#include "tilelink/coh/Table.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace octopus { namespace tl { namespace coh
{
    static std::string trim(const std::string &s)
    {
        // FSMReader::_trim: spaces, tabs, CR/LF
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
    }

    static std::vector<std::string> fields(const std::string &line)
    {
        // getline(..., ',') per field, as FSMReader does; a trailing comma gives no extra field.
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string x;
        while (std::getline(ss, x, ',')) f.push_back(trim(x));
        return f;
    }

    static bool isInteger(const std::string &s)
    {
        if (s.empty()) return false;
        for (char c : s) if (c < '0' || c > '9') return false;
        return true;
    }

    Table Table::load(const std::string &path)
    {
        std::ifstream f(path);
        if (!f) throw ConfigError("FSM table " + path + ": cannot open the file");
        return parse(f, path);
    }

    int Table::requireEvent(const std::string &n) const
    {
        int id = eventId(n);
        if (id < 0) throw ConfigError("FSM table " + m_name + ": no event named " + n);
        return id;
    }
    int Table::requireAction(const std::string &n) const
    {
        int id = actionId(n);
        if (id < 0) throw ConfigError("FSM table " + m_name + ": no action named " + n);
        return id;
    }
    int Table::requireState(const std::string &n) const
    {
        int id = stateId(n);
        if (id < 0) throw ConfigError("FSM table " + m_name + ": no state named " + n);
        return id;
    }

    bool Table::hasAction(int state, int event, int action) const
    {
        for (int a : transition(state, event).actions) if (a == action) return true;
        return false;
    }

    std::string Table::cellString(int state, int event) const
    {
        const Cell &c = transition(state, event);
        if (c.empty) return "";
        std::string s;
        for (int a : c.actions) s += actionName(a) + "/";
        if (c.actions.empty()) return stateName(c.next);           // bare next state
        if (c.next != state) s += stateName(c.next);                // `a/next`; `a/` = stay
        return s;
    }

    Table Table::parse(std::istream &in, const std::string &name)
    {
        Table t;
        t.m_name = name;
        enum Section { None, Events, Actions, States, Rows } sec = None;
        bool seen[4] = {false, false, false, false};
        std::vector<std::pair<int, std::vector<std::string>>> rows;   // (line, fields)
        std::vector<std::string> header_events;
        std::string line;
        int lineno = 0;
        auto err = [&](const std::string &what) -> ConfigError
        {
            return ConfigError("FSM table " + name + " line " + std::to_string(lineno) + ": " + what);
        };
        // One `id,name` list (events, actions, states): ids 0..n-1 in order, names unique.
        auto addId = [&](std::vector<std::string> &names, std::map<std::string, int> &ids, const std::vector<std::string> &f,
                         const char *what)
        {
            if (!isInteger(f[0])) throw err(std::string(what) + " number '" + f[0] + "' is not a non-negative integer");
            int id = std::atoi(f[0].c_str());
            if (id != int(names.size()))
                throw err(std::string(what) + " " + f[1] + " has number " + f[0] + ", expected " + std::to_string(names.size()) +
                          " (ids are positions: rows and columns are indexed by position)");
            if (ids.count(f[1])) throw err(std::string("duplicate ") + what + " name " + f[1]);
            ids[f[1]] = id;
            names.push_back(f[1]);
        };
        while (std::getline(in, line))
        {
            lineno++;
            std::vector<std::string> f = fields(line);
            std::string f0 = f.size() > 0 ? f[0] : "", f1 = f.size() > 1 ? f[1] : "";
            if (sec == Rows)
            {
                rows.push_back({lineno, f});
                continue;
            }
            // The section headers (FSMReader: EVENT_CSV_ID, ACTION_CSV_ID, STATE_CSV_ID, STATE_CSV_TABLE).
            if (f0 == "EventNum") { if (seen[0]) throw err("second EventNum section"); seen[0] = true; sec = Events; continue; }
            if (f0 == "ActionNum") { if (seen[1]) throw err("second ActionNum section"); seen[1] = true; sec = Actions; continue; }
            if (f0 == "StateNum") { if (seen[2]) throw err("second StateNum section"); seen[2] = true; sec = States; continue; }
            if (f0 == "State" && f1 == "stable")
            {
                if (!seen[0] || !seen[1] || !seen[2]) throw err("table header before the EventNum, ActionNum and StateNum sections");
                seen[3] = true;
                sec = Rows;
                for (size_t k = 3; k < f.size(); k++) header_events.push_back(f[k]);
                while (!header_events.empty() && header_events.back().empty()) header_events.pop_back();
                continue;
            }
            if (sec == None)
            {
                bool blank = true;
                for (auto &x : f) if (!x.empty()) blank = false;
                if (!blank) throw err("text before the first section: " + trim(line));
                continue;
            }
            // FSMReader::parseFile: a line whose second field is empty closes the section.
            if (f1.empty()) { sec = None; continue; }
            if (sec == Events) addId(t.m_events, t.m_event_ids, f, "event");
            else if (sec == Actions) addId(t.m_actions, t.m_action_ids, f, "action");
            else
            {
                std::vector<std::string> names;
                for (auto &r : t.m_states) names.push_back(r.name);
                addId(names, t.m_state_ids, f, "state");
                Row r;
                r.name = f[1];
                t.m_states.push_back(r);
            }
        }
        if (!seen[0]) throw ConfigError("FSM table " + name + ": no EventNum section");
        if (!seen[1]) throw ConfigError("FSM table " + name + ": no ActionNum section");
        if (!seen[2]) throw ConfigError("FSM table " + name + ": no StateNum section");
        if (!seen[3]) throw ConfigError("FSM table " + name + ": no table header (State,stable,isDataValid,...)");
        if (t.m_events.empty() || t.m_states.empty()) throw ConfigError("FSM table " + name + ": no events or no states");

        // The event columns are indexed by position (FSMReader: event_index++ per field), so the
        // header must list the events in EventNum order.
        if (header_events.size() != t.m_events.size())
            throw ConfigError("FSM table " + name + ": the table header has " + std::to_string(header_events.size()) +
                              " event columns, the EventNum section " + std::to_string(t.m_events.size()));
        for (size_t k = 0; k < header_events.size(); k++)
            if (header_events[k] != t.m_events[k])
                throw ConfigError("FSM table " + name + ": event column " + std::to_string(k) + " is " + header_events[k] +
                                  " but EventNum " + std::to_string(k) + " is " + t.m_events[k] + " (columns are indexed by position)");

        if (rows.size() != t.m_states.size())
        {
            // A trailing blank line would be read by FSMReader as a state named "" (§1.1).
            std::string why = rows.size() > t.m_states.size() ? " (a blank line after the last row is a row too)" : "";
            throw ConfigError("FSM table " + name + ": " + std::to_string(rows.size()) + " transition rows for " +
                              std::to_string(t.m_states.size()) + " states" + why);
        }
        for (size_t k = 0; k < rows.size(); k++)
        {
            lineno = rows[k].first;
            const std::vector<std::string> &f = rows[k].second;
            Row &r = t.m_states[k];
            std::string rowname = f.empty() ? "" : f[0];
            // FSMReader::getTransition indexes m_states[current_state]: the k-th row IS state k.
            if (rowname != r.name)
                throw err("row " + std::to_string(k) + " is state '" + rowname + "' but StateNum " + std::to_string(k) + " is " + r.name +
                          " (transition rows are indexed by position)");
            if (f.size() < 3) throw err("row " + r.name + ": missing the stable and isDataValid fields");
            if (f[1] != "0" && f[1] != "1") throw err("row " + r.name + ": stable must be 0 or 1, got '" + f[1] + "'");
            if (f[2] != "0" && f[2] != "1") throw err("row " + r.name + ": isDataValid must be 0 or 1, got '" + f[2] + "'");
            r.stable = f[1] == "1";
            r.data_valid = f[2] == "1";
            Cell stay;
            stay.next = int(k);   // an empty or missing (trailing) cell: stay, no action
            r.cells.assign(t.m_events.size(), stay);
            for (size_t e = 0; e < f.size() - 3; e++)
            {
                const std::string &text = f[e + 3];
                if (e >= t.m_events.size())
                {
                    if (text.empty()) continue;   // padding commas
                    throw err("row " + r.name + ": cell " + std::to_string(e) + " '" + text + "' beyond the last event column");
                }
                Cell &c = r.cells[e];
                c.text = text;
                c.next = int(k);
                if (text.empty()) continue;       // empty: stay, no action
                c.empty = false;
                const std::string where = "row " + r.name + ", event " + t.m_events[e] + ", cell '" + text + "'";
                size_t last = text.find_last_of('/');
                if (last == std::string::npos)
                {
                    // a bare next state
                    int s = t.stateId(text);
                    if (s < 0) throw err(where + ": unknown next state " + text);
                    c.next = s;
                    continue;
                }
                std::string nx = trim(text.substr(last + 1));
                if (!nx.empty())
                {
                    int s = t.stateId(nx);
                    if (s < 0) throw err(where + ": unknown next state " + nx);
                    c.next = s;
                }
                std::stringstream as(text.substr(0, last));
                std::string a;
                while (std::getline(as, a, '/'))
                {
                    a = trim(a);
                    int id = t.actionId(a);
                    if (a.empty()) throw err(where + ": empty action name");
                    if (id < 0) throw err(where + ": unknown action " + a);
                    c.actions.push_back(id);
                }
                if (c.actions.empty()) throw err(where + ": empty action name");
            }
        }
        return t;
    }
}}}
