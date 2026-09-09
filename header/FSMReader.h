/*
 * File  :      FSMREADER.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 15, 2021
 */

#ifndef _FSMREADER_H
#define _FSMREADER_H

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>

#define EVENT_CSV_ID "EventNum"
#define ACTION_CSV_ID "ActionNum"
#define STATE_CSV_ID "StateNum"
#define STATE_CSV_TABLE "State"

namespace octopus
{
    /**
 * brief FSMReader parses the CSV files that contain finite state machine, and
 * also the class provides methods to perfrom the FSM transitions.
 */

    class FSMReader
    {
    public:
        FSMReader(const std::string &fsmPath);

        int getState(const std::string &state_name);
        bool isValidState(int state);
        bool isHit(int state, int Event);
        bool isStall(int state, int Event);
        bool isStable(int state);

        void getTransition(int current_state, int event,
                            int& out_next_state, std::vector<int>& out_actions);

        // reverse lookups (id -> name) for readable coherence traces / debugging
        std::string getStateName(int id);
        std::string getEventName(int id);

    private:
        class FSMState
        {
        public:
            bool is_data_valid;
            bool stable;
            
            FSMState(const std::string &line,
                     const std::map<std::string, int> &eventIds,
                     const std::map<std::string, int> &actionIds,
                     const std::map<std::string, int> &stateIds);
            int getNextState(int event);
            const std::vector<int>& getActions(int event);
        private:
            //the transitions map uses keys to represent the event number and 
            //value to represent the next state number
            std::map<int, int> transitions;
            //the actions map uses keys to represent the event number, and
            //value is a vector of integers that represents the action(s) number
            std::map<int, std::vector<int>> actions;
            //this state's own index; used as the default next-state for any event
            //that has no explicit transition (an empty CSV cell = "stay in current
            //state"). Without this, a trailing empty cell -- dropped by getline --
            //left the transition unset and getNextState returned std::map's default
            //0 (= state I), silently mis-routing e.g. IM_dI + Invalidation to I.
            int m_own_state = 0;
        };

        std::map<std::string, int> m_eventIds;
        std::map<std::string, int> m_actionIds;
        std::map<std::string, int> m_stateIds;
        std::vector<FSMState> m_states;

        void parseFile(std::ifstream &file, std::map<std::string, int> &ids);
        void parseFile(std::ifstream &file, std::vector<FSMState> &states);
    };
}

#endif /* _FSMREADER_H */
