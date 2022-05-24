if(CONFIG_PCF85263)
    zephyr_include_directories(./)
	zephyr_library_sources(pcf85263.c)
endif()