/*
 * File  :      MultiCoreSim.cc
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 15, 2020
 */

#include "tinyxml.h"
#include "MCoreSimProjectXml.h"
#include "MCoreSimProject.h"
#include "FSMReader.h"
#include "CacheSim.h"
#include "CommandLine.h"

using namespace ns3;
using namespace std;

// NS_LOG_COMPONENT_DEFINE ("MultiCoreSimulator");

int main (int argc, char *argv[])
{
  // simulator output Trace Files
  string CpuTraceFile       = "CpuAckTrace";   // Dump CPU Ack data
  string CohCtrlsTraceFile  = "CohCtrlsTrace"; // Dump Internal states of 
                                               // CPU Mem Controllers

  // simulator input files
  string SimConfigFile = "test_cfg.xml"; // Configuration Parameter File
  string BMsPath = "BMs/tests/";         // Benchmark TraceFiles Path

  bool LogFileGenEnable = false;          // enable log file dumps

  // command line parser
  CommandLine cmd;

  // adding a call to our sim configurable parameters 
  cmd.AddValue("CfgFile", "simulator configuration file", SimConfigFile);
  cmd.AddValue("BMsPath", "benchmark trace file(s) path", BMsPath);
  cmd.AddValue("BusTraceFile", "trace file for bus transactions", CpuTraceFile);
  cmd.AddValue("CtrlsTraceFile", "trace file for coherence controlles", CohCtrlsTraceFile);
  cmd.AddValue("LogFileGenEnable", "enable flag for log file dump", LogFileGenEnable);

  // parse user commands
  cmd.Parse (argc, argv);

  CacheSim cache_sim(SimConfigFile.c_str(), BMsPath.c_str());
  cache_sim.run();
  cache_sim.join();
  return 0;
}
