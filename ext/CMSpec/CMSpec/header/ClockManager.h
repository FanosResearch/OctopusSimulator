/*
 * File  :      ClockManager.h
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 22, 2022
 */

#ifndef CLOCKMANAGER_H
#define CLOCKMANAGER_H

#include <iostream>
#include <vector>
#include <map>
#include <algorithm>

namespace ns3
{
    class ClockedObj
    {
        protected:
            uint64_t m_clk_period;

        public:
            ClockedObj(uint64_t clk_period);
            
            uint64_t getClkPeriod() {return m_clk_period;}
            virtual void cycleProcess() = 0;
            virtual void init() = 0;
    };

    class ClockManager
    {
    public:
        struct EventKey
        {
            public:
            uint64_t time_stamp;
            uint64_t obj_id;
            
            bool operator < (EventKey const& a) const
            {
                if(this->time_stamp < a.time_stamp)
                    return true;
                else if((this->time_stamp == a.time_stamp) && (this->obj_id < a.obj_id))
                    return true;
                else
                    return false;
            }
        };

    protected:
        bool clk_run;
        uint64_t current_time;

        std::vector<ClockedObj*> clock_triggers;
        std::map<EventKey, ClockedObj*> events; //time stamp is the key, and the value is a clocked object
        
        static ClockManager *_clock_manager;

        ClockManager();

    public:
        void registerCLKObj(uint64_t clk_period, ClockedObj* obj);
        void registerCLKTrigger(ClockedObj* obj);
        void init();
        void run();
        void clkStep();
        void stopClock(ClockedObj* obj);
        
        static ClockManager *getClockManager();
    };
}

#endif