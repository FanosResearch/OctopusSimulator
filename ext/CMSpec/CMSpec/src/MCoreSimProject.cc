/*
 * File  :      MCoreSimProject.cc
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 15, 2020
 */

#include "../header/MCoreSimProject.h"
// #include "simulator.h"

using namespace std;
using namespace ns3;

/*
 * Create the MCoreSimProject for the supplied configuration data
 */

MCoreSimProject::MCoreSimProject(MCoreSimProjectXml projectXmlCfg)
{
  // Set the project xml
  m_projectXmlCfg = projectXmlCfg;

  // Get clock frequency
  m_dt = projectXmlCfg.GetBusClkInNanoSec();
  m_busCycle = 0;

  // Get Run Till Sim End Flag
  m_runTillSimEnd = projectXmlCfg.GetRunTillSimEnd();

  // Get Simulation time to run
  // m_totalTimeInSeconds = (m_runTillSimEnd == true) ? std::numeric_limits<int>::max() : projectXmlCfg.GetTotalTimeInSeconds();

  // Enable Log File Generation
  m_logFileGenEnable = projectXmlCfg.GetLogFileGenEnable();


  // setup1(projectXmlCfg);
  setup2(projectXmlCfg);
}

MCoreSimProject::~MCoreSimProject()
{
  // for (CpuFIFO *cpuFIFO_ptr : m_cpuFIFO)
  // {
  //   delete cpuFIFO_ptr;
  // }
  // m_cpuFIFO.clear();

  // delete m_sharedCacheBusIfFIFO;
  // delete m_sharedCacheDRAMBusIfFIFO;
}

void MCoreSimProject::setup1(MCoreSimProjectXml projectXmlCfg)
{
  // initialize Simulator components
  // m_cpuCoreGens = list<Ptr<CpuCoreGenerator>>();
  // m_cpuFIFO = list<CpuFIFO *>();
  m_cpuCacheCtrl = list<CacheController *>();

  // Get all cpu configurations from xml
  list<CacheXml> xmlPrivateCaches = projectXmlCfg.GetPrivateCaches();
  list<CacheXml> xmlSharedCaches;

  CacheXml xmlSharedCache = projectXmlCfg.GetSharedCache();
  xmlSharedCaches.push_back(xmlSharedCache);

  // Get L1Bus configurations
  // L1BusCnfgXml L1BusCnfg = projectXmlCfg.GetL1BusCnfg();

  char path_array[256];
  getcwd (path_array, sizeof(path_array));

  string path(path_array);
  int app_name_index = path.rfind(APP_NAME);
  
  m_fsm_protocol_path = path.substr(0, app_name_index + sizeof(APP_NAME) - 1) + string("/Protocols_FSM/");
  m_fsm_llc_protocol_path = path.substr(0, app_name_index + sizeof(APP_NAME) - 1) + string("/Protocols_FSM/");
  cout <<"FSM path: " << m_fsm_protocol_path << endl;

  // Get Coherence protocol type
  GetCohrProtocolType();

  m_maxPendReq = 0;

  bus = new TripleBus(xmlPrivateCaches, xmlSharedCaches, projectXmlCfg.GetBusFIFOSize());

  // iterate over each core
  for (list<CacheXml>::iterator it = xmlPrivateCaches.begin(); it != xmlPrivateCaches.end(); it++)
  {
    CacheXml PrivateCacheXml = *it;

    /*
     * instantiate cpu FIFOs
     */
    // CpuFIFO *newCpuFIFO = new CpuFIFO(PrivateCacheXml.GetCacheId(), projectXmlCfg.GetCpuFIFOSize());
    // m_cpuFIFO.push_back(newCpuFIFO);

    // /*
    //  * instantiate cpu cores
    //  */
    // Ptr<CpuCoreGenerator> newCpuCore = CreateObject<CpuCoreGenerator>(newCpuFIFO);
    stringstream bmTraceFile, cpuTraceFile, ctrlTraceFile;
    bmTraceFile << projectXmlCfg.GetBMsPath() << "/trace_C" << PrivateCacheXml.GetCacheId() << ".trc.shared";
    // cpuTraceFile << projectXmlCfg.GetBMsPath() << "/" << projectXmlCfg.GetCpuTraceFile() << PrivateCacheXml.GetCacheId() << ".txt";
    // ctrlTraceFile << projectXmlCfg.GetBMsPath() << "/" << projectXmlCfg.GetCohCtrlsTraceFile() << PrivateCacheXml.GetCacheId() << ".txt";
    // double cpuClkPeriod = PrivateCacheXml.GetCpuClkNanoSec();
    // double cpuClkSkew = cpuClkPeriod * PrivateCacheXml.GetCpuClkSkew() / 100.00;
    // newCpuCore->SetCoreId(PrivateCacheXml.GetCacheId());
    // newCpuCore->SetBmFileName(bmTraceFile.str());
    // newCpuCore->SetCpuTraceFile(cpuTraceFile.str());
    // newCpuCore->SetCtrlsTraceFile(ctrlTraceFile.str());
    // newCpuCore->SetDt(cpuClkPeriod);
    // newCpuCore->SetClkSkew(cpuClkSkew);
    // newCpuCore->SetLogFileGenEnable(m_logFileGenEnable);
    // newCpuCore->SetOutOfOrderStages(projectXmlCfg.GetOutOfOrderStages());
    // m_cpuCoreGens.push_back(newCpuCore);

    // bm_paths.push_back(bmTraceFile.str());


    DirectInterconnect *cpu_interconnect = new DirectInterconnect(-1, it->GetCacheId(), projectXmlCfg.GetCpuFIFOSize());
    
    new CPU(PrivateCacheXml, projectXmlCfg.GetOutOfOrderStages(), cpu_interconnect->getInterfaceFor(-1), bmTraceFile.str());
        
    CommunicationInterface* bus_interface = bus->getInterfaceFor(PrivateCacheXml.GetCacheId());

    /*
     * instantiate cache controllers
     */
    CacheController *newCacheCtrl;
    if (m_cohrProt == CohProtType::SNOOP_MESI || m_cohrProt == CohProtType::SNOOP_MOESI)
      newCacheCtrl = new CacheControllerExclusive(PrivateCacheXml, m_fsm_protocol_path, bus_interface, 
                                                  cpu_interconnect->getInterfaceFor(it->GetCacheId()),
                                                  projectXmlCfg.GetCache2Cache(), xmlSharedCache.GetCacheId(), m_cohrProt);
    else
      newCacheCtrl = new CacheController(PrivateCacheXml, m_fsm_protocol_path, bus_interface, 
                                         cpu_interconnect->getInterfaceFor(it->GetCacheId()),
                                         projectXmlCfg.GetCache2Cache(), xmlSharedCache.GetCacheId(), m_cohrProt);

    m_cpuCacheCtrl.push_back(newCacheCtrl);

    if (m_maxPendReq < PrivateCacheXml.GetNPendReq())
    {
      m_maxPendReq = PrivateCacheXml.GetNPendReq();
    }
  }

  bus2 = new Bus(xmlSharedCaches, projectXmlCfg.GetDRAMId(), projectXmlCfg.GetBusFIFOSize(), bus->getLowerLevelIds());
  CommunicationInterface* LLC_bus_interface = bus->getInterfaceFor(xmlSharedCache.GetCacheId());
  CommunicationInterface* LLC_DRAM_interface = bus2->getInterfaceFor(xmlSharedCache.GetCacheId());

  m_SharedCacheCtrl = new CacheController_End2End(xmlSharedCache, m_fsm_llc_protocol_path, LLC_DRAM_interface, LLC_bus_interface,
                                          projectXmlCfg.GetCache2Cache(), projectXmlCfg.GetDRAMId(), m_llcCohrProt, bus->getLowerLevelIds());


  CommunicationInterface* DRAM_LLC_interface = bus2->getInterfaceFor(projectXmlCfg.GetDRAMId());
  m_main_memory = new MainMemoryController(projectXmlCfg, DRAM_LLC_interface, xmlSharedCache.GetCacheId());
  // m_mcsim_interface = new MCsimInterface(projectXmlCfg, DRAM_LLC_interface, xmlSharedCache.GetCacheId());

  Logger::getLogger()->registerReportPath(projectXmlCfg.GetBMsPath() + string("/newLogger"));   
  // if (L1BusCnfg.GetReqBusArb() == "RR" ||
  //     L1BusCnfg.GetReqBusArb() == "WRR" ||
  //     L1BusCnfg.GetReqBusArb() == "HRR")
  //   Logger::getLogger()->setReplacementCorrection(L1BusCnfg.GetRespBusLatcy());
  // else
  //   Logger::getLogger()->setReplacementCorrection(L1BusCnfg.GetRespBusLatcy());
}

void MCoreSimProject::setup2(MCoreSimProjectXml projectXmlCfg)
{
  // initialize Simulator components
  // m_cpuCoreGens = list<Ptr<CpuCoreGenerator>>();
  m_cpuCacheCtrl = list<CacheController *>();

  // Get all cpu configurations from xml
  list<CacheXml> xmlPrivateCaches = projectXmlCfg.GetPrivateCaches();
  list<CacheXml> xmlSharedCaches;

  CacheXml xmlSharedCache = projectXmlCfg.GetSharedCache();
  xmlSharedCaches.push_back(xmlSharedCache);

  // Get L1Bus configurations
  // L1BusCnfgXml L1BusCnfg = projectXmlCfg.GetL1BusCnfg();

  // char path_array[256];
  // getcwd (path_array, sizeof(path_array));

  // string path(path_array);
  // int app_name_index = path.rfind(APP_NAME);
  
  // m_fsm_protocol_path = path.substr(0, app_name_index + sizeof(APP_NAME) - 1) + string("/Protocols_FSM/");
  // m_fsm_llc_protocol_path = path.substr(0, app_name_index + sizeof(APP_NAME) - 1) + string("/Protocols_FSM/");
  m_fsm_protocol_path = "/home/gem5/gem5_new/ext/CMSpec/CMSpec/Protocols_FSM/";
  m_fsm_llc_protocol_path = "/home/gem5/gem5_new/ext/CMSpec/CMSpec/Protocols_FSM/";
  cout <<"FSM path: " << m_fsm_protocol_path << endl;

  // Get Coherence protocol type
  GetCohrProtocolType();

  bus = new TripleBus(xmlPrivateCaches, xmlSharedCaches, projectXmlCfg.GetBusFIFOSize());
  bus2 = new Bus(xmlSharedCaches, projectXmlCfg.GetDRAMId(), projectXmlCfg.GetBusFIFOSize(), bus->getLowerLevelIds());

  CommunicationInterface* LLC_bus_interface = bus->getInterfaceFor(xmlSharedCache.GetCacheId());
  CommunicationInterface* LLC_DRAM_interface = bus2->getInterfaceFor(xmlSharedCache.GetCacheId());

  m_SharedCacheCtrl = new CacheController_End2End(xmlSharedCache, m_fsm_llc_protocol_path, LLC_DRAM_interface, LLC_bus_interface,
                                          projectXmlCfg.GetCache2Cache(), projectXmlCfg.GetDRAMId(), m_llcCohrProt, bus->getLowerLevelIds());


  CommunicationInterface* DRAM_LLC_interface = bus2->getInterfaceFor(projectXmlCfg.GetDRAMId());
  m_main_memory = new MainMemoryController(projectXmlCfg, DRAM_LLC_interface, xmlSharedCache.GetCacheId());
  // ExternalMem* external_mem = new ExternalMem(projectXmlCfg, DRAM_LLC_interface, xmlSharedCache.GetCacheId());
  // ExternalMem::setExtMem(external_mem);

  // iterate over each core
  for (list<CacheXml>::iterator iter = xmlPrivateCaches.begin(); iter != xmlPrivateCaches.end(); iter++)
  {    
    DirectInterconnect *cpu_interconnect = new DirectInterconnect(-1, iter->GetCacheId(), projectXmlCfg.GetCpuFIFOSize());
    
    ExternalCPU* external_cpu = new ExternalCPU(*iter, cpu_interconnect->getInterfaceFor(-1));
    external_cpu->registerInitializableMemory(m_SharedCacheCtrl);
    ExternalCPU::getExtCPUs()->emplace(iter->GetCacheId(), external_cpu);
    
    CommunicationInterface* bus_interface = bus->getInterfaceFor(iter->GetCacheId());
    CacheController* private_cache;
    if (m_cohrProt == CohProtType::SNOOP_MESI || m_cohrProt == CohProtType::SNOOP_MOESI)
      private_cache = new CacheControllerExclusive(*iter, m_fsm_protocol_path, bus_interface, 
                                                  cpu_interconnect->getInterfaceFor(iter->GetCacheId()),
                                                  projectXmlCfg.GetCache2Cache(), xmlSharedCache.GetCacheId(), m_cohrProt);
    else
      private_cache = new CacheController(*iter, m_fsm_protocol_path, bus_interface, 
                                         cpu_interconnect->getInterfaceFor(iter->GetCacheId()),
                                         projectXmlCfg.GetCache2Cache(), xmlSharedCache.GetCacheId(), m_cohrProt);

    m_cpuCacheCtrl.push_back(private_cache);
    m_SharedCacheCtrl->m_children_controllers.push_back(private_cache);
    cpu_interconnect->init();
  }

  Logger::getLogger()->registerReportPath(projectXmlCfg.GetBMsPath() + string("/newLogger"));   
  // if (L1BusCnfg.GetReqBusArb() == "RR" ||
  //     L1BusCnfg.GetReqBusArb() == "WRR" ||
  //     L1BusCnfg.GetReqBusArb() == "HRR")
  //   Logger::getLogger()->setReplacementCorrection(L1BusCnfg.GetRespBusLatcy());
  // else
  //   Logger::getLogger()->setReplacementCorrection(L1BusCnfg.GetRespBusLatcy());
}

/*
 * start simulation engines
 */
void MCoreSimProject::Start()
{

  // for (list<Ptr<CpuCoreGenerator>>::iterator it = m_cpuCoreGens.begin(); it != m_cpuCoreGens.end(); it++)
  // {
  //   (*it)->init();
  // }


  for (list<ExternalCPU *>::iterator it = m_ext_cpu.begin(); it != m_ext_cpu.end(); it++)
  {
    (*it)->init();
  }

  for (list<CacheController *>::iterator it = m_cpuCacheCtrl.begin(); it != m_cpuCacheCtrl.end(); it++)
  {
    (*it)->init();
  }

  m_SharedCacheCtrl->init();
  // m_SharedCacheCtrl->initializeCacheData(bm_paths);

  // m_dramCtrl->init();
  m_main_memory->init();
  // m_mcsim_interface->init();

  // m_busArbiter->init();
  bus->init();
  bus2->init();

  // Simulator::Schedule(Seconds(0.0), &Step, this);
  // Simulator::Stop(MilliSeconds(m_totalTimeInSeconds));
}

void MCoreSimProject::Step(MCoreSimProject *project)
{
  project->CycleProcess();
}

void MCoreSimProject::CycleProcess()
{
  bool SimulationDoneFlag = true;

  // for (list<Ptr<CpuCoreGenerator>>::iterator it = m_cpuCoreGens.begin(); it != m_cpuCoreGens.end(); it++)
  // {
  //   SimulationDoneFlag &= (*it)->GetCpuSimDoneFlag();
  // }

  // if (SimulationDoneFlag == true && m_cpuCoreGens.size() > 0)
  // {
  //   cout << "Current Simulation Done at Bus Clock Cycle # " << m_busCycle << endl;
  //   // cout << "L2 Nmiss =  " << m_SharedCacheCtrl->GetShareCacheMisses() << endl;
  //   // cout << "L2 NReq =  " << m_SharedCacheCtrl->GetShareCacheNReqs() << endl;
  //   // cout << "L2 Miss Rate =  " << (m_SharedCacheCtrl->GetShareCacheMisses() / (float)m_SharedCacheCtrl->GetShareCacheNReqs()) * 100 << endl;
  //   exit(0);
  // }

  // Schedule the next run
  // Simulator::Schedule(NanoSeconds(m_dt), &MCoreSimProject::Step, this);
  m_busCycle++;
}

void MCoreSimProject::EnableDebugFlag(bool Enable)
{

  // for (list<Ptr<CacheController>>::iterator it = m_cpuCacheCtrl.begin(); it != m_cpuCacheCtrl.end(); it++)
  // {
  //   (*it)->SetLogFileGenEnable(Enable);
  // }

  // m_SharedCacheCtrl->SetLogFileGenEnable(Enable);
  // m_busArbiter->SetLogFileGenEnable(Enable);
}

void MCoreSimProject::GetCohrProtocolType()
{
  string cohType = m_projectXmlCfg.GetCohrProtType();
  if (cohType == "MSI")
  {
    m_cohrProt = CohProtType::SNOOP_MSI;
    m_llcCohrProt = CohProtType::SNOOP_LLC_MSI;
    m_fsm_protocol_path += "MSI_splitBus_snooping.csv";
    m_fsm_llc_protocol_path += "MSI_LLC.csv";
  }
  else if (cohType == "MESI")
  {
    m_cohrProt = CohProtType::SNOOP_MESI;
    m_llcCohrProt = CohProtType::SNOOP_LLC_MESI;
    m_fsm_protocol_path += "MESI_splitBus_snooping.csv";
    m_fsm_llc_protocol_path += "MESI_LLC.csv";
  }
  else if (cohType == "MOESI")
  {
    m_cohrProt = CohProtType::SNOOP_MOESI;
    m_llcCohrProt = CohProtType::SNOOP_LLC_MOESI;
    m_fsm_protocol_path += "MOESI_splitBus_snooping.csv";
    m_fsm_llc_protocol_path += "MOESI_LLC.csv";
  }
  else if (cohType == "PMSI")
  {
    m_cohrProt = CohProtType::SNOOP_PMSI;
    m_llcCohrProt = CohProtType::SNOOP_LLC_PMSI;
    m_fsm_protocol_path += "PMSI.csv";
    m_fsm_llc_protocol_path += "PMSI_LLC.csv";
  }
  else if (cohType == "PMESI")
  {
    m_cohrProt = CohProtType::SNOOP_PMESI;
    m_llcCohrProt = CohProtType::SNOOP_LLC_PMESI;
    m_fsm_protocol_path += "PMESI.csv";
    m_fsm_llc_protocol_path += "PMESI_LLC.csv";
  }
  else if (cohType == "PMSI_Asterisk")
  {
    m_cohrProt = CohProtType::SNOOP_PMSI_ASTERISK;
    m_llcCohrProt = CohProtType::SNOOP_LLC_PMSI_ASTERISK;
    m_fsm_protocol_path += "PMSI_asterisk.csv";
    m_fsm_llc_protocol_path += "PMSI_asterisk_LLC.csv";
  }
  else if (cohType == "PMESI_Asterisk")
  {
    m_cohrProt = CohProtType::SNOOP_PMESI_ASTERISK;
    m_llcCohrProt = CohProtType::SNOOP_LLC_PMESI_ASTERISK;
    m_fsm_protocol_path += "PMESI_asterisk.csv";
    m_fsm_llc_protocol_path += "PMESI_asterisk_LLC.csv";
  }
  else
  {
    std::cout << "Unsupported Coherence Protocol Cnfg Param = " << cohType << std::endl;
    exit(0);
  }
}
