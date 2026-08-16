add_library(CMSpec_core
   
            ClockManager.cpp
            ExternalCPU.cpp
            Logger.cpp
            Loggers.cpp
            CPU.cpp
            CacheDataHandler.cpp
            FSMReader.cpp
            CacheDataHandler_COTS.cpp
            GenericCacheLine.cpp
            # MCsimInterface.cpp
            CacheSim.cpp
            IdGenerator.cc
            MainMemoryController.cpp
            Configurable.cpp
            CLParser.cpp
            DebugPrint.cpp
            LoggerPredictable.cpp
    )

    add_subdirectory(Arbiters)
    add_subdirectory(CacheControllers)
    add_subdirectory(Interconnect)
    add_subdirectory(Protocols)
    add_subdirectory(ReplacementPolicies)
    add_subdirectory(SystemConfigurations)