/*
 * File  :      ClockManager.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 22, 2022
 */

#include "../header/ClockManager.h"

using namespace std;
namespace octopus
{

    ClockedObj::ClockedObj(uint64_t clk_period)
    {
        m_clk_period = clk_period;
        ClockManager::getClockManager()->registerCLKObj(m_clk_period, this);
    }

    ClockManager *ClockManager::_clock_manager = NULL;

    ClockManager::ClockManager()
    {
        current_time = 0;
        clk_run = false;
    }

    uint64_t ClockManager::obj_id = 0;
    void ClockManager::registerCLKObj(uint64_t clk_period, ClockedObj *obj)
    {
        // EventKey key = {.time_stamp = clk_period,
        //                 .obj_id = (uint64_t)obj};
        EventKey key = {.time_stamp = clk_period,
                        .obj_id = ClockManager::obj_id};
        events.insert(pair<EventKey, ClockedObj *>(key, obj));

        ClockManager::obj_id++;
    }

    void ClockManager::deregisterCLKObj(ClockedObj *obj)
    {
        // Remove obj's scheduled event so it is no longer dispatched every cycle.
        // Used by idle objects (e.g. a disabled DebugPrint) that register via the
        // ClockedObj base ctor but have no per-cycle work to do. One-time linear
        // scan at construction; obj appears at most once in the queue.
        for (auto it = events.begin(); it != events.end(); ++it)
        {
            if (it->second == obj)
            {
                events.erase(it);
                return;
            }
        }
    }

    void ClockManager::registerCLKTrigger(ClockedObj *obj)
    {
        clock_triggers.push_back(obj);
    }

    void ClockManager::init()
    {
        map<EventKey, ClockedObj *>::iterator itr;

        for (itr = events.begin(); itr != events.end(); itr++)
            itr->second->init();
    }

    void ClockManager::run()
    {
        int disp_delay = 0;
        clk_run = true;
        while (clk_run)
        {
            clkStep();
            if(disp_delay == 0)
                cout << "\rElapsed time: \t" << current_time;

            disp_delay = (disp_delay + 1) % 100;
        }
    }

    void ClockManager::clkStep()
    {
        do
        {
            map<EventKey, ClockedObj *>::iterator itr = events.begin();
            if (current_time < itr->first.time_stamp)
            {
                current_time = itr->first.time_stamp;
                return;
            }
            else if (current_time > itr->first.time_stamp)
            {
                cout << "ClockManager: CLK event is missed." << endl;
                exit(0);
            }

            auto node = events.extract(itr); // reuse the tree node: no free/malloc per event
            ClockedObj *obj = node.mapped();

            obj->cycleProcess(); // trigger the clock obj
            node.key().time_stamp = current_time + obj->getClkPeriod();
            events.insert(std::move(node));
        } while (true);
    }

    void ClockManager::stopClock(ClockedObj *obj)
    {
        vector<ClockedObj *>::iterator iter;
        iter = std::find(clock_triggers.begin(), clock_triggers.end(), obj);

        if (iter != clock_triggers.end())
            clock_triggers.erase(iter);

        if (clock_triggers.size() == 0)
            clk_run = false;
    }

    ClockManager *ClockManager::getClockManager()
    {
        if (ClockManager::_clock_manager == NULL)
            ClockManager::_clock_manager = new ClockManager();
        return ClockManager::_clock_manager;
    }
}