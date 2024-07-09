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
    string m_cohProtocol;
    int m_outOfOrderStages;

    list<CacheXml> m_L1Caches;
    list<CacheXml> m_L2Caches;
    CacheXml m_sharedCache;
    // L1BusCnfgXml m_L1BusCnfg;
    
    int m_dramSimEnable;
    int m_dramId;
    string m_dramModle;
    int m_dramLatcy;
    int m_dramOutstandReq;
    int m_dramctrlClkNanoSec;
    int m_dramctrlClkSkew; 
    
     
    // The name of the path used for Benchmark trace files
    string m_bmsPath;

    // trace file names
    string m_cpuTraceFile;
    string m_cohCtrlsTraceFile;

    // enable flag for LogFile
    bool m_logFileGenEnable;

public:

    list<CacheXml> GetL1Caches() {
        return m_L1Caches;
    }

    list<CacheXml> GetL2Caches() {
        return m_L2Caches;
    }

    void SetPrivateCaches(list<CacheXml> privateCaches) {
        m_L1Caches = privateCaches;
    }

    CacheXml GetSharedCache() {
       return m_sharedCache;
    }

    void SetSharedCache(CacheXml sharedCache) {
      m_sharedCache = sharedCache;
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
 
    // L1BusCnfgXml GetL1BusCnfg() {
    //    return m_L1BusCnfg;
    // }

    // void SetL1BusCnfg(L1BusCnfgXml l1BusCnfg) {
    //    m_L1BusCnfg = l1BusCnfg;
    // }

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
  
    int GetDRAMId () {
      return m_dramId;
    }
    
    int GetDRAMCtrlClkNanoSec () {
      return m_dramctrlClkNanoSec;
    }

    int GetDRAMCtrlClkSkew () {
      return m_dramctrlClkSkew;
    }
    
    string GetCohrProtType () {
      return m_cohProtocol;
    }  

    int GetOutOfOrderStages () {
      return m_outOfOrderStages;
    }  

    // load input configurations
    void LoadFromXml (TiXmlHandle root) {
       m_numberOfRuns       = 1;
       m_totalTimeInSeconds = 5;
       m_runTillSimEnd      = 0;
       m_busClkNanoSec      = 100;
       m_nCores             = 4;
       m_cpuFIFOSize        = 6;
       m_busFIFOSize        = 6;
       m_cach2Cache         = true;
       m_L1Caches      = list<CacheXml> ();
       m_sharedCache        = CacheXml ();
      //  m_L1BusCnfg          = L1BusCnfgXml ();
       m_dramSimEnable      = 0;
       m_dramOutstandReq    = 4;
       m_dramModle          = "FIXEDLat";
       m_dramLatcy          = 100;
       m_dramId             = 200;
       m_dramctrlClkNanoSec = 100;
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
          std::cout << "DEBUG COH Protocol Name in XML header: "<< m_cohProtocol << std::endl;
          rootPtr->QueryIntAttribute("OutOfOrderStages", &m_outOfOrderStages);
          
          // get interconnect configuration parameters
          TiXmlHandle interConnectRoot = root.FirstChildElement("InterConnect");
          TiXmlElement* interConnectRootPtr = interConnectRoot.Element();
          if (interConnectRootPtr) {
             TiXmlElement* L1BusCnfgPtr = interConnectRootPtr->FirstChildElement("L1BusCnfg");
             TiXmlHandle L1BusCnfgHandle = TiXmlHandle(L1BusCnfgPtr);
            //  m_L1BusCnfg.LoadFromXml(L1BusCnfgHandle);
          }
          
          // get L1 Cache Configuration parameters
          TiXmlHandle L1CachesRoot = root.FirstChildElement("L1Caches");
          TiXmlElement* L1CachesRootPtr = L1CachesRoot.Element();

          if (L1CachesRootPtr) {
             TiXmlElement* L1CachePtr = L1CachesRootPtr->FirstChildElement("L1Cache");
             for (; L1CachePtr; L1CachePtr = L1CachePtr->NextSiblingElement()) {
               CacheXml newL1Cache;
               TiXmlHandle L1CacheHandle = TiXmlHandle(L1CachePtr);
               newL1Cache.LoadFromXml(L1CacheHandle);
               m_L1Caches.push_back(newL1Cache);
             }
          }

          // get L2 Cache Configuration parameters
          TiXmlHandle L2CachesRoot = root.FirstChildElement("L2Caches");
          TiXmlElement* L2CachesRootPtr = L2CachesRoot.Element();

          if (L2CachesRootPtr) {
             TiXmlElement* L2CachePtr = L2CachesRootPtr->FirstChildElement("L2Cache");
             for (; L2CachePtr; L2CachePtr = L2CachePtr->NextSiblingElement()) {
               CacheXml newL2Cache;
               TiXmlHandle L2CacheHandle = TiXmlHandle(L2CachePtr);
               newL2Cache.LoadFromXml(L2CacheHandle);
               m_L2Caches.push_back(newL2Cache);
             }
          }

          TiXmlHandle sharedCachesRoot = root.FirstChildElement("sharedCaches");
          TiXmlElement* sharedCachesRootPtr = sharedCachesRoot.Element();

          if (sharedCachesRootPtr) {
             TiXmlElement* sharedCachePtr = sharedCachesRootPtr->FirstChildElement("sharedCache");
             TiXmlHandle sharedCacheHandle = TiXmlHandle(sharedCachePtr);
             m_sharedCache.LoadFromXml(sharedCacheHandle);
          }           
       
          TiXmlHandle DRAMCnfgRoot = root.FirstChildElement("DRAMCnfg");
          TiXmlElement* DRAMCnfgRootPtr = DRAMCnfgRoot.Element();
          if (DRAMCnfgRootPtr) {
            DRAMCnfgRootPtr->QueryIntAttribute   ("DRAMId", &m_dramId                       );
            DRAMCnfgRootPtr->QueryIntAttribute   ("DRAMSIMEnable", &m_dramSimEnable         );
            DRAMCnfgRootPtr->QueryStringAttribute("MEMMODLE", &m_dramModle                  );
            DRAMCnfgRootPtr->QueryIntAttribute   ("MEMLATENCY", &m_dramLatcy                );
            DRAMCnfgRootPtr->QueryIntAttribute   ("MEMOutsandingReqs", &m_dramOutstandReq   );
            DRAMCnfgRootPtr->QueryIntAttribute   ("ctrlClkNanoSec" , &m_dramctrlClkNanoSec  );
            DRAMCnfgRootPtr->QueryIntAttribute   ("ctrlClkSkew"    , &m_dramctrlClkSkew     );
          }
                          
       }
    } // void LoadFromXml

};

#endif /* _MCoreSimProjectXml_H */
