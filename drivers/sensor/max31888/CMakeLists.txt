if(CONFIG_MAX31888)
    zephyr_include_directories(./)
    zephyr_library_sources(max31888.c)
endif()
