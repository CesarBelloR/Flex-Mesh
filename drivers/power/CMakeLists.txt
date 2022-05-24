if(CONFIG_POWER_MODULE)
    zephyr_include_directories(./)
	zephyr_library_sources(power_management.c)
endif()