/*
 * File  :      DebugPrint.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Nov 30, 2023
 */

#ifndef _DebugPrint_H
#define _DebugPrint_H

#include "ClockManager.h"
#include "Configurable.h"

#include "CommunicationInterface.h"

#include <stdio.h>
#include <stdarg.h>
#include <map>

using namespace std;

namespace octopus
{
    class DebugPrint : public ClockedObj, Configurable
    {
    protected:
        string source_name;

        uint64_t m_clk_cycle;

        int enable;
        int print_preamble;

        int print_name, print_clk, print_msg_id;
        int print_addr, print_from, print_to;
        int print_owner;

        string addr_filter;
        vector<string> cond_addr;
        vector<int> cond_msg_id, cond_from, cond_owner;

        string target;
        FILE* file;
        static map<string, FILE*> unique_file;

        virtual void cycleProcess();
        virtual void init();

        void printPreamble(Message *msg);
        void _print(const char * format, ...);
        void _flush();
        bool condition(Message *msg = NULL);

    public:
        DebugPrint(ParametersMap map, string source_name = "",
                   string pname = "",
                   string config_path = string(CONFIGURATION_PATH),
                   string name = STRINGIFY(DebugPrint));
        ~DebugPrint();
    
        void print(Message *msg = NULL, const char * format = "", ...);
    };
}

#endif /* _DebugPrint_H */
