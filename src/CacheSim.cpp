/*
 * File  :      CacheSim.cpp
 * Author:      Mohammed Ismail
 * Email :      ismaim22@mcmaster.ca
 *
 * Created On Oct 4, 2022
 */

#include "../header/CacheSim.h"
#include "fstream"
#include "iostream"
#include <string>

namespace ns3
{
    CacheSim::CacheSim(const char *config_file_path, const char *output_logs_path)
    {
        string SimConfigFile = config_file_path;

  string line;
  ifstream myfile (config_file_path);
  if (myfile.is_open())
  {
    while ( getline (myfile,line) )
    {
      std::cout << line << '\n';
    }
    myfile.close();
  }

  else cout << "Unable to open file"; 


        TiXmlDocument doc(SimConfigFile.c_str());
        doc.LoadFile();

        TiXmlHandle hDoc(&doc);
        TiXmlElement *root = hDoc.FirstChildElement().Element();
        TiXmlHandle hroot = TiXmlHandle(root);

        MCoreSimProjectXml xml;
        xml.LoadFromXml(hroot);
        xml.SetBMsPath(string(output_logs_path));
        cout << "SA: output_logs_path "<< string(output_logs_path)<<endl;
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

    void CacheSim::step()
    {
        ClockManager::getClockManager()->clkStep();
    }


    void CacheSim::run()
    {
        // simulator_thread = new thread([](){
            ClockManager::getClockManager()->run();
        // });
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
