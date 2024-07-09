target_sources(CMSpec_core PRIVATE

            ${CMAKE_CURRENT_SOURCE_DIR}/Bus.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/BusController.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/BusInterface.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/DirectController.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/DirectInterconnect.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/Point2PointController.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/SplitBusController.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/TripleBus.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/TripleBusController.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/TripleBusInterface.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/UnifiedBusController.cpp
    )