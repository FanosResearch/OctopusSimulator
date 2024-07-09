target_sources(CMSpec_core PRIVATE

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
            ${CMAKE_CURRENT_SOURCE_DIR}/L1MSIProtocol.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/L2MSIProtocol.cpp
    )