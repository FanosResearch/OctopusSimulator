target_sources(Octopus_core PRIVATE

            ${CMAKE_CURRENT_SOURCE_DIR}/BaseController.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/CacheController.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/CacheControllerExclusive.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/CacheController_End2End.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/CacheControllerDirectory.cpp
    )