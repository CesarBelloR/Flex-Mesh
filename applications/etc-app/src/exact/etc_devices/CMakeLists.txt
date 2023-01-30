target_include_directories(app PRIVATE .)
target_sources(app PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/etc_logger.c
        ${CMAKE_CURRENT_SOURCE_DIR}/etc_device.c
        )