add_library(Octopus_core
   
            ClockManager.cpp
            tinyxmlparser.cpp
            ExternalCPU.cpp
            Logger.cpp
            CPU.cpp
            CacheDataHandler.cpp
            FSMReader.cpp
            MCoreSimProject.cc
            tinystr.cpp
            CacheDataHandler_COTS.cpp
            GenericCacheLine.cpp
            MCsimInterface.cpp
            tinyxml.cpp
            CacheSim.cpp
            IdGenerator.cc
            MainMemoryController.cpp
            tinyxmlerror.cpp
            CommandLine.cpp
            Configurable.cpp
            CLParser.cpp
            DebugPrint.cpp
    )

    add_subdirectory(Arbiters)
    add_subdirectory(CacheControllers)
    add_subdirectory(Interconnect)
    add_subdirectory(Protocols)
    add_subdirectory(ReplacementPolicies)
    add_subdirectory(SystemConfigurations)

    # MCsim DRAM simulator (vendored from grrof). Header-only schedulers under
    # MCsim/generalScheduler and MCsim/system are pulled via relative includes.
    add_subdirectory(MCsim/src)
    add_subdirectory(MCsim/dram)
    target_include_directories(Octopus_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/MCsim/src
        ${CMAKE_CURRENT_SOURCE_DIR}/MCsim/dram)