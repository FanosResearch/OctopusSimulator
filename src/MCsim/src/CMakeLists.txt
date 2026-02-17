target_sources(CMSpec_core PRIVATE
   
                ${CMAKE_CURRENT_SOURCE_DIR}/AddressMapping.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/CommandGenerator.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/CommandQueue.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/CommandScheduler.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/MemoryController.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/RequestQueue.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/RequestScheduler.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/Requestor.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/RequestorsQueues.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/ClockDomain.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/MemoryDevice.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/MemorySystem.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/MultiChannelMemorySystem.cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/SimulatorObject.cpp

    )


