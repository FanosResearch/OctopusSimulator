/*
 * File  :      MCoreSimProject.cc
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 15, 2020
 */

 #include "../header/MCoreSimProject.h"
 #include "../header/AddrMapping.h"
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
 
 
 
 
   setup1(projectXmlCfg);
   //setup2(projectXmlCfg);
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
 
   if(projectXmlCfg.GetmemSystem() == "RROF") // if Arbiter is RROF, enable global queues by default
   {
     projectXmlCfg.SetglobalQueues_en(1);
   }
   if (projectXmlCfg.GetglobalQueues_en()) // if GlobalQeueues enables, enable MCsim by default as queues are part of MCsim
   {
     projectXmlCfg.Setmcsim_En(1);
   }
 
   m_globalQueues_en = projectXmlCfg.GetglobalQueues_en();
 
   m_llc_nbnks = projectXmlCfg.GetLLCBnk();
 
   AddrMapping::getAddrMapping()->set_partition_setup(projectXmlCfg.GetLLCPartition());
   AddrMapping::getAddrMapping()->set_llc_nbnks(projectXmlCfg.GetLLCBnk());
   AddrMapping::getAddrMapping()->set_ncores(projectXmlCfg.GetNumPrivCore());
 
   // m_cpuCoreGens = list<Ptr<CpuCoreGenerator>>();
   // m_cpuFIFO = list<CpuFIFO *>();
   m_cpuCacheCtrl = list<CacheController *>();
 
   // Get all cpu configurations from xml
   list<CacheXml> xmlPrivateCaches = projectXmlCfg.GetPrivateCaches();
   list<CacheXml> xmlSharedCaches = projectXmlCfg.GetSharedCaches();
 
   //CacheXml xmlSharedCache = projectXmlCfg.GetSharedCache();
   //xmlSharedCaches.push_back(xmlSharedCache);
 
   // iterate over each LLC xml and get their IDs
   vector <int> xmlSharedCacheIDs;
   CacheXml SharedCacheXml;
   for (list<CacheXml>::iterator it = xmlSharedCaches.begin(); it != xmlSharedCaches.end(); it++)
   {
     SharedCacheXml = *it;
     SharedCacheXml.SetglobalQueues_en(m_globalQueues_en);
     SharedCacheXml.SetLogEnable(projectXmlCfg.GetLogFileGenEnable());
     SharedCacheXml.SetLLCBnk(m_llc_nbnks);
     xmlSharedCacheIDs.push_back (SharedCacheXml.GetCacheId());
   }
   AddrMapping::getAddrMapping()->set_cl_size(xmlSharedCaches.begin()->GetBlockSize());
   AddrMapping::getAddrMapping()->set_bnk_size(xmlSharedCaches.begin()->GetCacheSize());
   AddrMapping::getAddrMapping()->set_nway(xmlSharedCaches.begin()->GetNWays());
 
   // Get L1Bus configurations
   // L1BusCnfgXml L1BusCnfg = projectXmlCfg.GetL1BusCnfg();
 
   char path_array[256];
   getcwd (path_array, sizeof(path_array));
   string path(path_array);
   int app_name_index = path.rfind(APP_NAME);
   
   m_fsm_protocol_path = string("/workspaces/CMSpec/CMSpec/Protocols_FSM/");
   m_fsm_llc_protocol_path = string("/workspaces/CMSpec/CMSpec/Protocols_FSM/");
   cout <<"FSM path: " << m_fsm_protocol_path << endl;
 
   // Get Coherence protocol type
   GetCohrProtocolType();
 
   bus = new TripleBus(xmlPrivateCaches, xmlSharedCaches, projectXmlCfg.GetBusFIFOSize());
 
   int order = 0;
   // iterate over each core
   for (list<CacheXml>::iterator it = xmlPrivateCaches.begin(); it != xmlPrivateCaches.end(); it++)
   {
     // /*
     //  * instantiate cpu cores
     //  */
     // Ptr<CpuCoreGenerator> newCpuCore = CreateObject<CpuCoreGenerator>(newCpuFIFO);
 
     CacheXml PrivateCacheXml = *it;
     PrivateCacheXml.SetglobalQueues_en(m_globalQueues_en);
     PrivateCacheXml.SetLogEnable(m_logFileGenEnable);
     PrivateCacheXml.SetLLCBnk(m_llc_nbnks);
 
     stringstream bmTraceFile; //, cpuTraceFile, ctrlTraceFile;
     bmTraceFile << projectXmlCfg.GetBMsPath() << "/trace_C" << PrivateCacheXml.GetCacheId() << ".trc.shared";
 
     DirectInterconnect *cpu_interconnect = new DirectInterconnect(-1, it->GetCacheId(), projectXmlCfg.GetCpuFIFOSize());
     
     ExternalCPU* external_cpu = new ExternalCPU(*it, cpu_interconnect->getInterfaceFor(-1));
     ExternalCPU::getExtCPUs()->emplace(it->GetCacheId(), external_cpu);
     //new CPU(PrivateCacheXml, projectXmlCfg.GetOutOfOrderStages(), cpu_interconnect->getInterfaceFor(-1), bmTraceFile.str());
         
     CommunicationInterface* bus_interface = bus->getInterfaceFor(PrivateCacheXml.GetCacheId());
 
     /*
      * instantiate cache controllers
      */
     CacheController *newCacheCtrl;
     if (m_cohrProt == CohProtType::SNOOP_MESI || m_cohrProt == CohProtType::SNOOP_MOESI)
       newCacheCtrl = new CacheControllerExclusive(PrivateCacheXml, m_fsm_protocol_path, bus_interface, 
                                                   cpu_interconnect->getInterfaceFor(it->GetCacheId()),
                                                   projectXmlCfg.GetCache2Cache(), xmlSharedCacheIDs, m_cohrProt, -1);
     else
       newCacheCtrl = new CacheController(PrivateCacheXml, m_fsm_protocol_path, bus_interface, 
                                          cpu_interconnect->getInterfaceFor(it->GetCacheId()),
                                          projectXmlCfg.GetCache2Cache(), xmlSharedCacheIDs, m_cohrProt, -1);
 
     m_cpuCacheCtrl.push_back(newCacheCtrl);
   }
 
   bus2 = new Bus(xmlSharedCaches, projectXmlCfg.GetDRAMId()[0], projectXmlCfg.GetBusFIFOSize(), bus->getLowerLevelIds());
 
   // iterate over each LLC
   for (list<CacheXml>::iterator it = xmlSharedCaches.begin(); it != xmlSharedCaches.end(); it++)
   {
 
     CommunicationInterface* LLC_bus_interface = bus->getInterfaceFor(it->GetCacheId());
     CommunicationInterface* LLC_DRAM_interface = bus2->getInterfaceFor(it->GetCacheId());
     CacheController *newCacheCtrl;
     it->SetglobalQueues_en(m_globalQueues_en);
     it->SetLogEnable(projectXmlCfg.GetLogFileGenEnable());
     it->SetLLCBnk(m_llc_nbnks);
     newCacheCtrl = new CacheController_End2End(*it, m_fsm_llc_protocol_path, LLC_DRAM_interface, LLC_bus_interface,
                                           projectXmlCfg.GetCache2Cache(), projectXmlCfg.GetDRAMId(), m_llcCohrProt, order, bus->getLowerLevelIds());
     order++;
     m_SharedCacheCtrl.push_back(newCacheCtrl);
   }
 
   CommunicationInterface* DRAM_LLC_interface = bus2->getInterfaceFor(projectXmlCfg.GetDRAMId()[0]);
   if(m_projectXmlCfg.Getmcsim_En())
     m_mcsim_interface = new MCsimInterface(projectXmlCfg, DRAM_LLC_interface, xmlSharedCacheIDs);
   else
     m_main_memory = new MainMemoryController(projectXmlCfg, DRAM_LLC_interface, xmlSharedCacheIDs);
    
   Logger::getLogger()->registerReportPath(projectXmlCfg.GetLoggerPath()); 
   Logger::getLogger()->setLogEnable(projectXmlCfg.GetLogFileGenEnable());
 }
 
 void MCoreSimProject::setup2(MCoreSimProjectXml projectXmlCfg)
 {
   /*
   // initialize Simulator components
   // m_cpuCoreGens = list<Ptr<CpuCoreGenerator>>();
   m_cpuCacheCtrl = list<CacheController *>();
 
   // Get all cpu configurations from xml
   list<CacheXml> xmlPrivateCaches = projectXmlCfg.GetPrivateCaches();
   list<CacheXml> xmlSharedCaches = projectXmlCfg.GetSharedCache();
 
   //CacheXml xmlSharedCache = projectXmlCfg.GetSharedCache();
   //xmlSharedCaches.push_back(xmlSharedCache);
 
   vector <int> xmlSharedCacheIDs;
   // iterate over each LLC xml 
   for (list<CacheXml>::iterator it = xmlSharedCaches.begin(); it != xmlSharedCaches.end(); it++)
   {
     CacheXml SharedCacheXml = *it;
     xmlSharedCacheIDs.push_back (it->GetCacheId());
   }
 
   // Get L1Bus configurations
   // L1BusCnfgXml L1BusCnfg = projectXmlCfg.GetL1BusCnfg();
 
   // char path_array[256];
   // getcwd (path_array, sizeof(path_array));
 
   // string path(path_array);
   // int app_name_index = path.rfind(APP_NAME);
   
   // m_fsm_protocol_path = path.substr(0, app_name_index + sizeof(APP_NAME) - 1) + string("/Protocols_FSM/");
   // m_fsm_llc_protocol_path = path.substr(0, app_name_index + sizeof(APP_NAME) - 1) + string("/Protocols_FSM/");
   m_fsm_protocol_path = "/workspaces/gem5_cachesimulator/ext/CMSpec/CMSpec/Protocols_FSM/";
   m_fsm_llc_protocol_path = "/workspaces/gem5_cachesimulator/ext/CMSpec/CMSpec/Protocols_FSM/";
   cout <<"FSM path: " << m_fsm_protocol_path << endl;
 
   // Get Coherence protocol type
   GetCohrProtocolType();
 
   bus = new TripleBus(xmlPrivateCaches, xmlSharedCaches, projectXmlCfg.GetBusFIFOSize());
   bus2 = new Bus(xmlSharedCaches, projectXmlCfg.GetDRAMId(), projectXmlCfg.GetBusFIFOSize(), bus->getLowerLevelIds());
 
   CommunicationInterface* LLC_bus_interface = bus->getInterfaceFor(xmlSharedCaches.begin()->GetCacheId());
   CommunicationInterface* LLC_DRAM_interface = bus2->getInterfaceFor(xmlSharedCaches.begin()->GetCacheId());
 
   m_SharedCacheCtrl[0] = new CacheController_End2End(xmlSharedCaches.begin(), m_fsm_llc_protocol_path, LLC_DRAM_interface, LLC_bus_interface,
     projectXmlCfg.GetCache2Cache(), projectXmlCfg.GetDRAMId(), m_llcCohrProt, bus->getLowerLevelIds());
 
 
   CommunicationInterface* DRAM_LLC_interface = bus2->getInterfaceFor(projectXmlCfg.GetDRAMId()[0]);
   m_main_memory = new MainMemoryController(projectXmlCfg, DRAM_LLC_interface, xmlSharedCacheIDs);
   // ExternalMem* external_mem = new ExternalMem(projectXmlCfg, DRAM_LLC_interface, xmlSharedCache.GetCacheId());
   // ExternalMem::setExtMem(external_mem);
 
   // iterate over each core
   for (list<CacheXml>::iterator iter = xmlPrivateCaches.begin(); iter != xmlPrivateCaches.end(); iter++)
   {    
     DirectInterconnect *cpu_interconnect = new DirectInterconnect(-1, iter->GetCacheId(), projectXmlCfg.GetCpuFIFOSize());
     
     ExternalCPU* external_cpu = new ExternalCPU(*iter, cpu_interconnect->getInterfaceFor(-1));
     external_cpu->registerInitializableMemory(m_SharedCacheCtrl[0]);
     ExternalCPU::getExtCPUs()->emplace(iter->GetCacheId(), external_cpu);
     
     CommunicationInterface* bus_interface = bus->getInterfaceFor(iter->GetCacheId());
     CacheController* private_cache;
     if (m_cohrProt == CohProtType::SNOOP_MESI || m_cohrProt == CohProtType::SNOOP_MOESI)
       private_cache = new CacheControllerExclusive(*iter, m_fsm_protocol_path, bus_interface, 
                                                   cpu_interconnect->getInterfaceFor(iter->GetCacheId()),
                                                   projectXmlCfg.GetCache2Cache(), xmlSharedCacheIDs, m_cohrProt);
     else
       private_cache = new CacheController(*iter, m_fsm_protocol_path, bus_interface, 
                                          cpu_interconnect->getInterfaceFor(iter->GetCacheId()),
                                          projectXmlCfg.GetCache2Cache(), xmlSharedCacheIDs, m_cohrProt);
 
     m_cpuCacheCtrl.push_back(private_cache);
     m_SharedCacheCtrl[0]->m_children_controllers.push_back(private_cache);
     cpu_interconnect->init();
   }
 
   Logger::getLogger()->registerReportPath(projectXmlCfg.GetBMsPath() + string("/newLogger"));   
   // if (L1BusCnfg.GetReqBusArb() == "RR" ||
   //     L1BusCnfg.GetReqBusArb() == "WRR" ||
   //     L1BusCnfg.GetReqBusArb() == "HRR")
   //   Logger::getLogger()->setReplacementCorrection(L1BusCnfg.GetRespBusLatcy());
   // else
   //   Logger::getLogger()->setReplacementCorrection(L1BusCnfg.GetRespBusLatcy());
   */
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
 
   for (list<CacheController *>::iterator it = m_SharedCacheCtrl.begin(); it != m_SharedCacheCtrl.end(); it++)
   {
     (*it)->init();
   }
 
   //m_SharedCacheCtrl->init();
   // m_SharedCacheCtrl->initializeCacheData(bm_paths);
 
   // m_dramCtrl->init();
   m_main_memory->init();
   m_mcsim_interface->init();
 
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
 