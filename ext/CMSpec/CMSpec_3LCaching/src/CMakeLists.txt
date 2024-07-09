add_library(CMSpec_core
   
            ClockManager.cpp
            tinyxmlparser.cpp
            CacheController_End2End.cpp
            ExternalCPU.cpp
            Logger.cpp
            CPU.cpp
            CacheDataHandler.cpp
            FSMReader.cpp
            MCoreSimProject.cc
            tinystr.cpp
            CacheController.cpp
            CacheDataHandler_COTS.cpp
            GenericCacheLine.cpp
        #     MCsimInterface.cpp
            tinyxml.cpp
            CacheControllerExclusive.cpp
            CacheSim.cpp
            IdGenerator.cc
            MainMemoryController.cpp
            tinyxmlerror.cpp
            CommandLine.cpp
            ExternalMem.cpp
    )

    add_subdirectory(Arbiters)
    add_subdirectory(Interconnect)
    add_subdirectory(Protocols)
    add_subdirectory(ReplacementPolicies)