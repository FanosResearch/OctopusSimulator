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
#include "FSMReader.h"

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

        // lets callers skip building a trace string when tracing is disabled
        bool enabled() const { return enable != 0; }
        // THE hook for a coherence transition (every protocol calls it right after
        // getTransition): prints the readable "old --event--> new" line when this
        // debugger is enabled (subject to its cond_* filters), and records the binary
        // Role::FSM event in the raw trace when OCTOPUS_TRACE is set (docs/Trace.md).
        // A Stall row leaves the line untouched, so it is reported as old --event--> old.
        void transition(Message *msg, uint32_t comp, int old_state, int event_id, int new_state, bool stalled, FSMReader *fsm);
    };
}

#endif /* _DebugPrint_H */
