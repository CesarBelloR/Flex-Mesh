if(CONFIG_BQ24195)
    zephyr_include_directories(./)
	zephyr_library_sources(bq24195.c)
endif()