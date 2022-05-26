target_sources_ifdef(CONFIG_DATE_TIME app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/date_time.c)

zephyr_include_directories(./)