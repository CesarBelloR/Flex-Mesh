target_include_directories(app PRIVATE .)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sensor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/watchdog.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rtc_calib.c
)