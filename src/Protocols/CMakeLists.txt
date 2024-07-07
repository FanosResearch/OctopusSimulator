target_sources(Octopus_core PRIVATE

            ${CMAKE_CURRENT_SOURCE_DIR}/CoherenceProtocolHandler.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/LLCMESIProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/LLCMSIProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/LLCPMESIProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/LLCPMSIProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MESIProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MOESIProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MSIProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/PMESIAsteriskProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/PMESIProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/PMSIAsteriskProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/PMSIProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MSIDirectory.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/LLCMSIDirectory.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MESIDirectory.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/LLCMESIDirectory.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MOESIDirectory.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/LLCMOESIDirectory.cpp
    )