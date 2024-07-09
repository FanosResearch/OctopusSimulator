/*
 * File  :      PrivateCacheXml.h
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 15, 2020
 */

#ifndef _CacheXml_H
#define _CacheXml_H
#include "tinyxml.h"
#include <string.h>

using namespace std;

class CacheXml {
private:
  int  m_cacheId;
  int  m_cpuClkNanoSec;
  int  m_cpuClkSkew;
  int  m_ctrlClkNanoSec;
  int  m_ctrlClkSkew;
  int  m_reqWbRatio;
  int  m_blockSize;    // in bytes
  int  m_cacheSize;    // in bytes
  int  m_mappingType;  // 0 = direct, 1 = associative 
  int  m_nways;        // number of ways, should be 1 in direct map
  int m_nPendingReq;
  string m_replcPolicy;
  int m_cachePreload;
  int dataAccessLatency;
  
public:

  void SetCacheId (int cacheId) {
     m_cacheId = cacheId;
  }

  int GetCacheId () {
     return m_cacheId;
  }

  void SetCpuClkNanoSec (int cpuClkNanoSec) {
     m_cpuClkNanoSec = cpuClkNanoSec;
  }

  int GetCpuClkNanoSec () {
     return m_cpuClkNanoSec;
  }

  int GetCtrlClkNanoSec () {
     return m_ctrlClkNanoSec;
  }

  int GetCpuClkSkew () {
     return m_cpuClkSkew;
  }

  int GetCtrlClkSkew () {
     return m_ctrlClkSkew;
  }

  int GetReqWbRatio () {
     return m_reqWbRatio;
  }


  void SetBlockSize (int blockSize) {
     m_blockSize = blockSize;
  }

  int GetBlockSize () {
     return m_blockSize;
  }

  void SetCacheSize (int cacheSize) {
     m_cacheSize = cacheSize;
  }

  int GetCacheSize () {
     return m_cacheSize;
  }

  void SetMappingType (int mappingType) {
     m_mappingType = mappingType;
  }

  int GetMappingType () {
     return m_mappingType;
  }

  void SetNWays (int nways) {
     m_nways = nways;
  }

  int GetNWays () {
     return m_nways;
  }
  
  string GetReplcPolicy () {
     return m_replcPolicy;
  }

  int GetNPendReq () {
     return m_nPendingReq;
  }
  
  int GetCachePreLoad () {
    return m_cachePreload;
  }

  int GetDataAccessLatency () {
    return dataAccessLatency;
  }
  
  void LoadFromXml(TiXmlHandle root) {

     // default values
     m_cacheId         = 1;
     m_cpuClkNanoSec   = 100;
     m_cpuClkSkew      = 0;
     m_ctrlClkNanoSec  = 100;
     m_ctrlClkSkew     = 0;
     m_reqWbRatio      = 50;
     m_blockSize       = 64;
     m_cacheSize       = 16384;
     m_mappingType     = 0; 
     m_nways           = 1;
     m_replcPolicy     = "RANDOM";
     m_nPendingReq     = 1;
     m_cachePreload    = 0;
     dataAccessLatency = 0;
     
     TiXmlElement* CacheRootPtr = root.Element();
     CacheRootPtr->QueryIntAttribute   ("cacheId"          , &m_cacheId         );
     CacheRootPtr->QueryIntAttribute   ("cpuClkNanoSec"    , &m_cpuClkNanoSec   );
     CacheRootPtr->QueryIntAttribute   ("cpuClkSkew"       , &m_cpuClkSkew      );
     CacheRootPtr->QueryIntAttribute   ("ctrlClkNanoSec"   , &m_ctrlClkNanoSec  );
     CacheRootPtr->QueryIntAttribute   ("ctrlClkSkew"      , &m_ctrlClkSkew     );
     CacheRootPtr->QueryIntAttribute   ("NPendReq"         , &m_nPendingReq     );     
     CacheRootPtr->QueryIntAttribute   ("reqWbRatio"       , &m_reqWbRatio      );
     CacheRootPtr->QueryStringAttribute("ReplcPolc"        , &m_replcPolicy     );
     CacheRootPtr->QueryIntAttribute   ("blockSize"        , &m_blockSize       );
     CacheRootPtr->QueryIntAttribute   ("cacheSize"        , &m_cacheSize       );
     CacheRootPtr->QueryIntAttribute   ("mapping"          , &m_mappingType     );
     CacheRootPtr->QueryIntAttribute   ("nways"            , &m_nways           );
     CacheRootPtr->QueryIntAttribute   ("CachePreLoad"     , &m_cachePreload    );
     CacheRootPtr->QueryIntAttribute   ("dataAccessLatency", &dataAccessLatency );
  }

};

#endif /* _CacheXml_H */
