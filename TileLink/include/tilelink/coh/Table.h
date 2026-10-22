/*
 * File  :      Table.h
 * The reader of the Octopus FSM CSV format (docs/TileLink_Plan.md §3.7: the library reads the
 * format itself, no Octopus dependency). It parses the same files the same way as Octopus's
 * src/FSMReader.cpp (docs/TileLink_TLC_Tables.md §1.1):
 *   - four sections, each introduced by a header line: `EventNum,Event`, `ActionNum,Action`,
 *     `StateNum,State` (each closed by a line whose second field is empty), then the table whose
 *     header is `State,stable,isDataValid,<one column per event in EventNum order>`;
 *   - transition rows are indexed by POSITION: the k-th row after the table header is the state
 *     with StateNum k; event columns are indexed by position too (EventNum order);
 *   - a cell is `action/action/.../next_state`: the text after the last '/' is the next state,
 *     empty means stay (`Fault/`, `Hit/`); a cell with no '/' is a bare next state with no
 *     action (`I`); an empty cell means stay with no action; whitespace is trimmed.
 * Where FSMReader silently accepts a malformed file (an unknown name throws std::out_of_range, a
 * row out of position is read as the wrong state, a trailing blank line becomes a state named ""),
 * this reader refuses it with ConfigError naming the file, the line and the row.
 */
#ifndef _TL_COH_TABLE_H
#define _TL_COH_TABLE_H
#include "tilelink/Types.h"
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace octopus { namespace tl { namespace coh
{
    // One cell: the actions in order and the next state (resolved: "stay" is the row's own state).
    struct Cell
    {
        std::vector<int> actions;
        int  next = -1;
        bool empty = true;     // the CSV cell was empty (stay, no action)
        std::string text;      // the CSV text, trimmed
    };

    class Table
    {
    public:
        Table() = default;
        // Reads a table file; throws ConfigError naming the file and the line on any malformation.
        static Table load(const std::string &path);
        // The same from a stream; `name` is used in the error messages.
        static Table parse(std::istream &in, const std::string &name);

        const std::string &name() const { return m_name; }
        int numStates() const { return int(m_states.size()); }
        int numEvents() const { return int(m_events.size()); }
        int numActions() const { return int(m_actions.size()); }

        // Name -> id, -1 when absent.
        int eventId(const std::string &n) const { return find(m_event_ids, n); }
        int actionId(const std::string &n) const { return find(m_action_ids, n); }
        int stateId(const std::string &n) const { return find(m_state_ids, n); }
        // As above, but ConfigError naming the table when absent (used by the policy engine to
        // resolve the names its decoders and builders rely on).
        int requireEvent(const std::string &n) const;
        int requireAction(const std::string &n) const;
        int requireState(const std::string &n) const;
        const std::string &eventName(int id) const { return m_events.at(size_t(id)); }
        const std::string &actionName(int id) const { return m_actions.at(size_t(id)); }
        const std::string &stateName(int id) const { return m_states.at(size_t(id)).name; }

        bool stable(int state) const { return m_states.at(size_t(state)).stable; }
        bool dataValid(int state) const { return m_states.at(size_t(state)).data_valid; }

        // FSMReader::getTransition: the cell of (state, event).
        const Cell &transition(int state, int event) const { return m_states.at(size_t(state)).cells.at(size_t(event)); }
        bool hasAction(int state, int event, int action) const;
        // The cell re-serialized from its parsed form (`a1/a2/next`, `a/` for stay, bare `next`,
        // "" for an empty cell): equal to text() for every well-formed cell.
        std::string cellString(int state, int event) const;

    private:
        struct Row
        {
            std::string name;
            bool stable = false, data_valid = false;
            std::vector<Cell> cells;
        };
        static int find(const std::map<std::string, int> &m, const std::string &n)
        {
            auto it = m.find(n);
            return it == m.end() ? -1 : it->second;
        }

        std::string m_name;
        std::vector<std::string> m_events, m_actions;
        std::map<std::string, int> m_event_ids, m_action_ids, m_state_ids;
        std::vector<Row> m_states;
    };
}}}
#endif /* _TL_COH_TABLE_H */
