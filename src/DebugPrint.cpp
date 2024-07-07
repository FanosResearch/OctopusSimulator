/*
 * File  :      DebugPrint.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 30, 2023
 */

#include "../header/DebugPrint.h"

using namespace std;

namespace octopus
{
    map<string, FILE*> DebugPrint::unique_file;

    DebugPrint::DebugPrint(ParametersMap map, string source_name, string pname, string config_path, string name) : 
            ClockedObj(0), Configurable(map, config_path, name, pname), source_name(source_name) 
    {
        //Parameters initialization
        m_clk_period = std::get<int>(parameters.at(STRINGIFY(m_clk_period)).value);
        target = std::get<string>(parameters.at(STRINGIFY(target)).value);
        
        enable = std::get<int>(parameters.at(STRINGIFY(enable)).value);
        print_preamble = std::get<int>(parameters.at(STRINGIFY(print_preamble)).value);

        print_name = std::get<int>(parameters.at(STRINGIFY(print_name)).value);
        print_clk = std::get<int>(parameters.at(STRINGIFY(print_clk)).value);
        print_msg_id = std::get<int>(parameters.at(STRINGIFY(print_msg_id)).value);
        print_addr = std::get<int>(parameters.at(STRINGIFY(print_addr)).value);
        print_from = std::get<int>(parameters.at(STRINGIFY(print_from)).value);
        print_to = std::get<int>(parameters.at(STRINGIFY(print_to)).value);
        print_owner = std::get<int>(parameters.at(STRINGIFY(print_owner)).value);

        addr_filter = std::get<string>(parameters.at(STRINGIFY(addr_filter)).value);
        cond_addr = std::get<vector<string>>(parameters.at(STRINGIFY(cond_addr)).value);
        cond_msg_id = std::get<vector<int>>(parameters.at(STRINGIFY(cond_msg_id)).value);
        cond_from = std::get<vector<int>>(parameters.at(STRINGIFY(cond_from)).value);
        cond_owner = std::get<vector<int>>(parameters.at(STRINGIFY(cond_owner)).value);

        //Constructor
        m_clk_cycle = 1;

        if(!target.empty())
        {   
            if(DebugPrint::unique_file.find(target) != DebugPrint::unique_file.end())
                file = DebugPrint::unique_file[target];
            else
            {
                file = fopen(target.c_str(), "w+");
                DebugPrint::unique_file[target] = file;
            }
        }
        else
            file = NULL;
    }
    
    DebugPrint::~DebugPrint()
    {
    }
    
    void DebugPrint::init()
    {
        if(enable)
        {
            if(print_preamble)
            {
                if(print_name)
                    _print("Name,");
                if(print_clk)
                    _print("Clock,");
                if(print_msg_id)
                    _print("Msg_id,");
                if(print_addr)
                    _print("Addr,");
                if(print_from)
                    _print("From,");
                if(print_to)
                    _print("to,");
                if(print_owner)
                    _print("Owner,");
            }
            
            _print("Message,");
            _print("\n");
            _flush();
        }
    }
    
    void DebugPrint::cycleProcess()
    {
        m_clk_cycle++;
    }

    void DebugPrint::print(Message *msg, const char * format, ...)
    {
        va_list args;
        if(!enable || !condition(msg))
            return;
        
        if(print_preamble)
            printPreamble(msg);

        va_start(args, format);        
        if(target.empty())
            vprintf(format, args);
        else
            vfprintf(file, format, args);
        va_end(args);

        _print(",\n");
        _flush();
    }
    
    void DebugPrint::printPreamble(Message *msg)
    {
        if(print_name)
            _print("%s,", source_name.c_str());
        if(print_clk)
            _print("%lld,", m_clk_cycle);
     
        if(msg)
        {
            if(print_msg_id)
                _print("%lld,", msg->msg_id);
            if(print_addr)
                _print("%lld,", msg->addr);
            if(print_from)
                _print("%lld,", msg->from);
            if(print_to)
            {
                _print("[");
                for(int i = 0; i < msg->to.size(); i++)
                {
                    if(i < (msg->to.size()-1))
                        _print("%lld-", msg->to[i]);
                    else
                        _print("%lld", msg->to[i]);
                }
                _print("],");
            }
            if(print_owner)
                _print("%lld,", msg->owner);
        }
        else
        {
            if(print_msg_id)
                _print(",");
            if(print_addr)
                _print(",");
            if(print_from)
                _print(",");
            if(print_to)
                _print(",");
            if(print_owner)
                _print(",");
        }
    }

    bool DebugPrint::condition(Message *msg)
    {
        bool cond[5] = {true, true, true, true, true}; //first one is false if any condition is on, then each entry is for a condition

        if(msg == NULL)
            return true;
        
        for(string addr : cond_addr)
        {
            uint64_t mask = std::stoull(addr_filter, NULL, 16);
            uint64_t test = std::stoull(addr);
            cond[0] = false;
            cond[1] = false;
            if((msg->addr & mask) == std::stoull(addr))
            {
                cond[1] = true;
                break;
            }
        }

        for(int id : cond_msg_id)
        {
            cond[0] = false;
            cond[2] = false;
            if((int)msg->msg_id == id)
            {
                cond[2] = true;
                break;
            }
        }

        for(int from : cond_from)
        {
            cond[0] = false;
            cond[3] = false;
            if((int)msg->from == from)
            {
                cond[3] = true;
                break;
            }
        }

        for(int owner : cond_owner)
        {
            cond[0] = false;
            cond[4] = false;
            if((int)msg->owner == owner)
            {
                cond[4] = true;
                break;
            }
        }

        return cond[0] | (cond[1] & cond[2] & cond[3] & cond[4]);
    }

    void DebugPrint::_print(const char * format, ...)
    {
        va_list args;

        va_start(args, format);

        if(target.empty())
            vprintf(format, args);
        else
            vfprintf(file, format, args);

        va_end(args);
    }

    void DebugPrint::_flush()
    {
        if(target.empty())
            fflush(stdout);
        else
            fflush(file);
    }
}