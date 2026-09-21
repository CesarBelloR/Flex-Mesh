target_include_directories(app PRIVATE .)
target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/etc_sensor.c
    ${CMAKE_CURRENT_SOURCE_DIR}/etc_sensor_helper.c
    ${CMAKE_CURRENT_SOURCE_DIR}/etc_sensor_calibration_load.c
)
target_sources_ifdef(CONFIG_ETC_SENSOR_SIM_SHELL app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/etc_sensor_sim.c
)
