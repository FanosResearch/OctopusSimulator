/*
 * File  :      FSMREADER.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On June 15, 2021
 */

#include "../header/FSMReader.h"

using namespace std;
namespace ns3
{
    FSMReader::FSMReader(const string &fsmPath)
    {
        string line;
        ifstream fsmFile(fsmPath);

        if (!fsmFile.is_open())
        {
            cout << "ERROR: Can't open FSM CSV file" << endl;
            exit(0);
        }

        while (getline(fsmFile, line))
        {
            if (line.find(EVENT_CSV_ID) < string::npos)
                this->parseFile(fsmFile, this->m_eventIds);
            else if (line.find(ACTION_CSV_ID) < string::npos)
                this->parseFile(fsmFile, this->m_actionIds);
            else if (line.find(STATE_CSV_ID) < string::npos)
                this->parseFile(fsmFile, this->m_stateIds);
            else if (line.find(STATE_CSV_TABLE) < string::npos)
                this->parseFile(fsmFile, this->m_states);
        }
        fsmFile.close();
    }

    void FSMReader::parseFile(ifstream &file, map<string, int> &ids)
    {
        string line;
        while (getline(file, line))
        {
            stringstream strStream(line);
            string index, value;

            getline(strStream, value, ','); //First field in the CSV line
            getline(strStream, index, ','); //Second field in the CSV line

            if (index.length() == 0)
                break;
            ids[index] = atoi(value.c_str());
        }
    }

    void FSMReader::parseFile(ifstream &file, vector<FSMState> &states)
    {
        string line;
        while (getline(file, line))
            states.push_back(FSMState(line, this->m_eventIds, this->m_actionIds, this->m_stateIds));
    }

    int FSMReader::getState(const std::string &state_name)
    {
        return m_stateIds[state_name];
    }

    void FSMReader::getTransition(int current_state, int event, int& out_next_state, std::vector<int>& out_actions)
    {
        out_next_state = this->m_states[current_state].getNextState(event);
        out_actions = this->m_states[current_state].getActions(event);
    }

    bool FSMReader::isValidState(int state)
    {
        return this->m_states[state].is_data_valid;
    }

    bool FSMReader::isHit(int state, int Event)
    {
        vector<int> actions = this->m_states[state].getActions(Event);
        int hit_id = this->m_actionIds["Hit"];
        for(int action : actions)
        {
            if(action == hit_id)
                return true;
        }
        return false;
    }

    bool FSMReader::isStall(int state, int Event)
    {
        vector<int> actions = this->m_states[state].getActions(Event);
        int stall_id = this->m_actionIds["Stall"];
        for(int action : actions)
        {
            if(action == stall_id)
                return true;
        }
        return false;
    }

    bool FSMReader::isStable(int state)
    {
        return this->m_states[state].stable;
    }

    /****************************************************** FSMReader::FSMState ******************************************************/

    FSMReader::FSMState::FSMState(const string &line, const map<string, int> &eventIds,
                                  const map<string, int> &actionIds, const map<string, int> &stateIds)
    {
        stringstream str_stream(line);
        string field;
        string state_name;
        int event_index = 0;

        getline(str_stream, state_name, ','); //state name

        getline(str_stream, field, ','); //is the state stable or not
        this->stable = (atoi(field.c_str()) == 1);

        getline(str_stream, field, ','); //is data valid field
        this->is_data_valid = (atoi(field.c_str()) == 1);

        while (getline(str_stream, field, ','))
        {
            field.erase(field.find('\r') != string::npos ? field.find('\r') : field.length());
            if (field.length() != 0)
            {
                auto last_delim_pos = string::npos;
                if ((last_delim_pos = field.find_last_of('/')) != string::npos)
                {
                    string next_state = field.substr(last_delim_pos + 1);
                    if (next_state.length() == 0) //if there is no next state, next state will be the current state
                        this->transitions[event_index] = stateIds.at(state_name);
                    else
                        this->transitions[event_index] = stateIds.at(next_state);

                    stringstream field_stream(field);
                    string action_name;
                    do
                    {
                        getline(field_stream, action_name, '/');
                        this->actions[event_index].push_back(actionIds.at(action_name));
                    } while (field_stream.tellg() < (long)last_delim_pos);
                }
                else //if there is no '/' in the field, the field will contain the next state only
                    this->transitions[event_index] = stateIds.at(field);
            }
            else
                this->transitions[event_index] = stateIds.at(state_name);

            event_index++;
        }
    }

    int FSMReader::FSMState::getNextState(int event)
    {
        return this->transitions[event];
    }
    
    const std::vector<int>& FSMReader::FSMState::getActions(int event)
    {
        return this->actions[event];
    }
}