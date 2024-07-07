target_sources(Octopus_core PRIVATE

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
            ${CMAKE_CURRENT_SOURCE_DIR}/Mesh.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MeshController.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/MeshInterface.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/NoC.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/NoCController.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/NoCInterface.cpp
    )