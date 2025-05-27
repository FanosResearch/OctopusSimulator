/*
 * File  :      MCoreSimProject.h
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 15, 2020
 */

 #ifndef _MCoreSimProject_H
 #define _MCoreSimProject_H
 
 #include "MCoreSimProjectXml.h"
 // #include "CpuCoreGenerator.h"
 #include "ExternalCPU.h"
 #include "ExternalMem.h"
 #include "CacheController.h"
 #include "CacheControllerExclusive.h"
 #include "CacheController_End2End.h"
 #include "Logger.h"
 #include "Bus.h"
 #include "TripleBus.h"
 #include "DirectInterconnect.h"
 #include "CommunicationInterface.h"
 #include "MainMemoryController.h"
 #include "CPU.h"
 #include "MCsimInterface.h"
 
 #include <string>
 #include <unistd.h>
 
 #define APP_NAME    "cachesim"
 
 using namespace std;
 using namespace ns3;
 
 class MCoreSimProject {
 private:
     // Shared Bus Max Clk Frequency 
     // int m_busClkMHz;
 
     // The Simulation Time Step
     double m_dt;
 
     // Simulation Time in seconds
     double m_totalTimeInSeconds;
 
     // Run 2 End Flag
     bool m_runTillSimEnd;
 
     // Enable Log File dump
     bool m_logFileGenEnable;
 
     // Enable sotring/removal from MCsim::globalQueues
     int m_globalQueues_en;
 
     // bus clk count
     uint64_t m_busCycle;
     
     // coherence protocol type
     CohProtType m_cohrProt;
     CohProtType m_llcCohrProt;
     string m_fsm_protocol_path;
     string m_fsm_llc_protocol_path;
     int m_llc_nbnks;
     
     int m_maxPendReq;
     
     // A list of Cpu Core generators
     std::list<ExternalCPU*> m_ext_cpu;
 
     // A list of Cache Ctrl engines
     std::list<CacheController*> m_cpuCacheCtrl;
 
     // A pointer to shared cache controller engine
     //CacheController* m_SharedCacheCtrl;
     std::list<CacheController*> m_SharedCacheCtrl;
 
     MainMemoryController* m_main_memory;
     MCsimInterface* m_mcsim_interface;
  
 
     Bus* bus;
     Bus* bus2;
 
     // The Xml documents for simulation configuration
     MCoreSimProjectXml m_projectXmlCfg;
 
     std::vector<std::string> bm_paths;
     
     void GetCohrProtocolType ();
     
      // cycle process 
      void CycleProcess  ();
      
      // enable debug flag 
      void EnableDebugFlag(bool Enable);
 public:
     // Constructor
     MCoreSimProject(MCoreSimProjectXml projectXmlCfg);
     ~MCoreSimProject();
 
     // Starts MCore simulation
     void Start ();
 
     // Calls the next step on the simulation
     static void Step (MCoreSimProject* project);
 
     void setup1(MCoreSimProjectXml projectXmlCfg);
     void setup2(MCoreSimProjectXml projectXmlCfg);
 
 };
 
 #endif /* _MCoreSimProject_H */
 