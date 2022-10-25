if(CONFIG_DS2484)
    zephyr_include_directories(./)
	zephyr_library_sources(ds2484.c)
endif()