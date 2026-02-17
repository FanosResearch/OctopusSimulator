/*
 * File  :      MCoreSimProjectXml.h
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 15, 2020
 */

#ifndef _MCoreSimProjectXml_H
#define _MCoreSimProjectXml_H

#include <list>
#include <stdlib.h>
#include <string.h>
#include "tinyxml.h"
#include "CacheXml.h"
#include <vector>

using namespace std;

class MCoreSimProjectXml {
private:
    int  m_numberOfRuns;
    int  m_totalTimeInSeconds;
    int  m_runTillSimEnd;
    int  m_busClkNanoSec;
    int  m_nCores;
    int  m_cpuFIFOSize;
    int  m_busFIFOSize;
    int  m_cach2Cache;
    string m_memSystem;
    string m_cohProtocol;
    string m_sysPath;
    string m_loggerPath;
    int m_outOfOrderStages;

    list<CacheXml> m_privateCaches;
    list<CacheXml> m_sharedCaches;
    
    int m_dramSimEnable;
    vector <int> m_dramId;
    string m_dramModle;
    int m_dramLatcy;
    int m_dramOutstandReq;
    double m_dramctrlClkNanoSec;
    int m_dramctrlClkSkew; 
    
     
    // The name of the path used for Benchmark trace files
    string m_bmsPath;

    // trace file names
    string m_cpuTraceFile;
    string m_cohCtrlsTraceFile;

    // enable flag for LogFile
    bool m_logFileGenEnable;

public:

    list<CacheXml> GetPrivateCaches() {
        return m_privateCaches;
    }

    void SetPrivateCaches(list<CacheXml> privateCaches) {
        m_privateCaches = privateCaches;
    }

    list <CacheXml> GetSharedCache() {
       return m_sharedCaches;
    }

    void SetSharedCache(list<CacheXml> sharedCache) {
      m_sharedCaches = sharedCache;
    }
  
    void SetBMsPath (string fileName) {
      m_bmsPath = fileName;
    }

    string GetBMsPath () {
      return m_bmsPath;
    }

    void SetCohCtrlsTraceFile (string fileName) {
      m_cohCtrlsTraceFile = fileName;
    }

    string GetCohCtrlsTraceFile () {
      return m_cohCtrlsTraceFile;
    }

    void SetCpuTraceFile (string fileName) {
      m_cpuTraceFile = fileName;
    }

    void SetLogFileGenEnable (bool logFileGenEnable ) {
      m_logFileGenEnable = logFileGenEnable;
    }

    bool GetLogFileGenEnable () {
      return m_logFileGenEnable;
    }

    string GetCpuTraceFile () {
      return m_cpuTraceFile;
    }

    int GetCpuFIFOSize () {
      return m_cpuFIFOSize;
    }

    int GetBusFIFOSize () {
      return m_busFIFOSize;
    }

    int GetNumberOfRuns() {
      return m_numberOfRuns;
    }

    void SetNumberOfRuns (int numberOfRuns) {
       m_numberOfRuns = numberOfRuns;
    }

    int GetBusClkInNanoSec() {
      return m_busClkNanoSec;
    }

    void SetBusClkInNanoSec(int busClkNanoSec) {
       m_busClkNanoSec = busClkNanoSec;
    }

    int GetTotalTimeInSeconds() {
      return m_totalTimeInSeconds;
    }

    void SetTotalTimeInSeconds(int totalTimeInSeconds) {
       m_totalTimeInSeconds = totalTimeInSeconds;
    }

    int GetRunTillSimEnd() {
      return m_runTillSimEnd;
    }

    int GetNumPrivCore () {
      return m_nCores;
    }

    int GetCache2Cache () {
      return m_cach2Cache;
    }
    
    int GetDRAMSimEnable () {
      return m_dramSimEnable;
    }

    int GetDRAMFixedLatcy () {
      return m_dramLatcy;
    }      

    string GetDRAMModle () {
      return m_dramModle;
    }
    
    int GetDRAMOutstandReq () {
      return m_dramOutstandReq;
    }
  
    vector <int> GetDRAMId () {
      return m_dramId;
    }
    
    double GetDRAMCtrlClkNanoSec () {
      return m_dramctrlClkNanoSec;
    }

    int GetDRAMCtrlClkSkew () {
      return m_dramctrlClkSkew;
    }
    
    string GetCohrProtType () {
      cout<<"m_cohProtocol= "<<m_cohProtocol<<endl;
      return m_cohProtocol;
    }  

    string GetsysPath () {
      return m_sysPath;
    }

    string GetmemSystem () {
      return m_memSystem;
    }  
    
    string GetLoggerPath () {
      return m_loggerPath;
    } 

    int GetOutOfOrderStages () {
      return m_outOfOrderStages;
    }  

    // load input configurations
    void LoadFromXml (TiXmlHandle root) {
       m_numberOfRuns       = 1;
       m_totalTimeInSeconds = 5;
       m_runTillSimEnd      = 0;
       m_busClkNanoSec      = 1;
       m_nCores             = 4;
       m_cpuFIFOSize        = 6;
       m_busFIFOSize        = 6;
       m_cach2Cache         = true;
       m_privateCaches      = list<CacheXml> ();
       m_sharedCaches       = list<CacheXml> ();
       m_dramSimEnable      = 0;
       m_dramOutstandReq    = 4;
       m_dramModle          = "FIXEDLat";
       m_dramLatcy          = 100;
       m_dramId.push_back( 200);
       m_dramctrlClkNanoSec = 1;
       m_dramctrlClkSkew    = 0;
       
       // read configuration parameters from xml file
       TiXmlElement* rootPtr = root.Element();

       if (rootPtr != NULL) {
       
          // get global configuration parameters
          rootPtr->QueryIntAttribute("numberOfRuns", &m_numberOfRuns);
          rootPtr->QueryIntAttribute("totalTimeInSeconds", &m_totalTimeInSeconds);
          rootPtr->QueryIntAttribute("RunTillEnd", &m_runTillSimEnd);
          rootPtr->QueryIntAttribute("busClkNanoSec", &m_busClkNanoSec);
          rootPtr->QueryIntAttribute("nCores", &m_nCores);
          rootPtr->QueryIntAttribute("cpuFIFOSize", &m_cpuFIFOSize);
          rootPtr->QueryIntAttribute("busFIFOSize", &m_busFIFOSize);
          rootPtr->QueryIntAttribute("Cache2Cache", &m_cach2Cache );     
          rootPtr->QueryStringAttribute("CohProtocol", &m_cohProtocol); 
          rootPtr->QueryStringAttribute("sysPath", &m_sysPath);
          rootPtr->QueryStringAttribute("memSystem", &m_memSystem); 
          rootPtr->QueryStringAttribute("loggerPath", &m_loggerPath); 
          std::cout << "DEBUG COH Protocol Name in XML header: "<< m_cohProtocol << std::endl;
          rootPtr->QueryIntAttribute("OutOfOrderStages", &m_outOfOrderStages);
          
          // get interconnect configuration parameters
          TiXmlHandle interConnectRoot = root.FirstChildElement("InterConnect");
          TiXmlElement* interConnectRootPtr = interConnectRoot.Element();
          if (interConnectRootPtr) {
             TiXmlElement* L1BusCnfgPtr = interConnectRootPtr->FirstChildElement("L1BusCnfg");
             TiXmlHandle L1BusCnfgHandle = TiXmlHandle(L1BusCnfgPtr);
          }
          
          // get L1 Cache Configuration parameters
          TiXmlHandle privateCachesRoot = root.FirstChildElement("privateCaches");
          TiXmlElement* privateCachesRootPtr = privateCachesRoot.Element();

          if (privateCachesRootPtr) {
             TiXmlElement* privateCachePtr = privateCachesRootPtr->FirstChildElement("privateCache");
             for (; privateCachePtr; privateCachePtr = privateCachePtr->NextSiblingElement()) {
               CacheXml newPrivateCache;
               TiXmlHandle privateCacheHandle = TiXmlHandle(privateCachePtr);
               newPrivateCache.LoadFromXml(privateCacheHandle);
               m_privateCaches.push_back(newPrivateCache);
             }
          }

          TiXmlHandle sharedCachesRoot = root.FirstChildElement("sharedCaches");
          TiXmlElement* sharedCachesRootPtr = sharedCachesRoot.Element();

          if (sharedCachesRootPtr) {
             //TiXmlElement* sharedCachePtr = sharedCachesRootPtr->FirstChildElement("sharedCache");
             //TiXmlHandle sharedCacheHandle = TiXmlHandle(sharedCachePtr);
             //m_sharedCache.LoadFromXml(sharedCacheHandle);

            TiXmlElement* sharedCachePtr = sharedCachesRootPtr->FirstChildElement("sharedCache");
            for (; sharedCachePtr; sharedCachePtr = sharedCachePtr->NextSiblingElement()) {
              CacheXml newSharedCache;
              TiXmlHandle sharedCacheHandle = TiXmlHandle(sharedCachePtr);
              newSharedCache.LoadFromXml(sharedCacheHandle);
              m_sharedCaches.push_back(newSharedCache);
            }
          }           
       
          TiXmlHandle DRAMCnfgRoot = root.FirstChildElement("DRAMCnfg");
          TiXmlElement* DRAMCnfgRootPtr = DRAMCnfgRoot.Element();
          if (DRAMCnfgRootPtr) {
            DRAMCnfgRootPtr->QueryIntAttribute   ("DRAMId", &m_dramId[0]                       );
            DRAMCnfgRootPtr->QueryIntAttribute   ("DRAMSIMEnable", &m_dramSimEnable         );
            DRAMCnfgRootPtr->QueryStringAttribute("MEMMODLE", &m_dramModle                  );
            DRAMCnfgRootPtr->QueryIntAttribute   ("MEMLATENCY", &m_dramLatcy                );
            DRAMCnfgRootPtr->QueryIntAttribute   ("MEMOutsandingReqs", &m_dramOutstandReq   );
            //DRAMCnfgRootPtr->QueryIntAttribute   ("ctrlClkNanoSec" , &m_dramctrlClkNanoSec  );
            DRAMCnfgRootPtr->QueryIntAttribute   ("ctrlClkSkew"    , &m_dramctrlClkSkew     );
          }
                          
       }
       else 
           cout<<"TiXmlElement* rootPtr is NULL\n";
    } // void LoadFromXml

};

#endif /* _MCoreSimProjectXml_H */
