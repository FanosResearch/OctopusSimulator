/*
 * File  :      CacheSim.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 4, 2022
 */

#include "../header/CacheSim.h"

namespace ns3
{
    CacheSim::CacheSim(const char *config_file_path, const char *output_logs_path)
    {
        TiXmlDocument doc(config_file_path);
        doc.LoadFile();

        TiXmlHandle hDoc(&doc);
        TiXmlElement *root = hDoc.FirstChildElement().Element();
        TiXmlHandle hroot = TiXmlHandle(root);

        MCoreSimProjectXml xml;
        xml.LoadFromXml(hroot);
        xml.SetBMsPath(string(output_logs_path));

        printf("%s\n", config_file_path);
        
        // setup simulation environment
        project = new MCoreSimProject(xml);

        // set simulation clock to one nano-Second
        // clock resolution is the smallest time value
        // that can be respresented in our simulator
        // Time::SetResolution(Time::NS); // MS, US, PS

        // initialize the simulator
        // project->Start();
        ClockManager::getClockManager()->init();

        // simulator_thread = NULL;
    }

    CacheSim::~CacheSim()
    {
        delete project;
    }

    void CacheSim::run()
    {
        // simulator_thread = new thread([](){
            ClockManager::getClockManager()->run();
        // });
    }

    void CacheSim::step()
    {
        ClockManager::getClockManager()->clkStep();
    }
    
    void CacheSim::join()
    {
        // if(simulator_thread != NULL)
        //     simulator_thread->join();
        // else
        // {
        //     cout << "Fatal error: the simulator thread was created properly!!" << endl;
        //     exit(0);
        // }
    }
}