/*
 * File  :      ClockManager.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Dec 22, 2022
 */

#include "../header/ClockManager.h"
#include "../header/CommunicationInterface.h"

using namespace std;
namespace ns3
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

    void ClockManager::registerCLKObj(uint64_t clk_period, ClockedObj *obj)
    {
        EventKey key = {.time_stamp = clk_period,
                        .obj_id = (uint64_t)obj};
        events.insert(pair<EventKey, ClockedObj *>(key, obj));
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

            EventKey key = itr->first;
            ClockedObj *obj = itr->second;
            events.erase(itr);

            obj->cycleProcess(); // trigger the clock obj
            key.time_stamp = current_time + obj->getClkPeriod();
            events.insert(pair<EventKey, ClockedObj *>(key, obj));
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