if(CONFIG_DS18S20)
    zephyr_include_directories(./)
	zephyr_library_sources(ds18s20.c)
endif()