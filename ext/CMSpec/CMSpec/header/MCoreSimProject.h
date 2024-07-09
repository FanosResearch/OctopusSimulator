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
// #include "MCsimInterface.h"

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

    // bus clk count
    uint64_t m_busCycle;
    
    // coherence protocol type
    CohProtType m_cohrProt;
    CohProtType m_llcCohrProt;
    string m_fsm_protocol_path;
    string m_fsm_llc_protocol_path;
    
    int m_maxPendReq;
    
    // A list of Cpu Core generators
    // std::list<ns3::Ptr<ns3::CpuCoreGenerator> > m_cpuCoreGens;
    std::list<ExternalCPU*> m_ext_cpu;
    
    // A list of Cpu buffers
    // std::list<ns3::CpuFIFO*> m_cpuFIFO;

    // A list of Cache Ctrl engines
    std::list<CacheController*> m_cpuCacheCtrl;

    // A list of Cache Ctrl Bus interface buffers
    // std::list<ns3::BusIfFIFO*> m_busIfFIFO;

    // A pointer to shared cache controller engine
    CacheController* m_SharedCacheCtrl;

    MainMemoryController* m_main_memory;
    // MCsimInterface* m_mcsim_interface;
    // // A pointer to shared cache Bus IF buffers
    // BusIfFIFO* m_sharedCacheBusIfFIFO;

    // // A pointer to shared cache Bus IF buffers
    // DRAMIfFIFO* m_sharedCacheDRAMBusIfFIFO;
    
    // // A pointer to DRAM Controller engine
    // ns3::Ptr<ns3::DRAMCtrl> m_dramCtrl;
    
    // // Interconnect FIFOs:
    // ns3::Ptr<ns3::InterConnectFIFO> m_interConnectFIFO;

    // // A pointer to Bus Arbiter
    // ns3::Ptr<ns3::BusArbiter> m_busArbiter;
    Bus* bus;
    Bus* bus2;

    // A pointer to Latency Logger component
    // std::list<ns3::Ptr<ns3::LatencyLogger> > m_latencyLogger;

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
