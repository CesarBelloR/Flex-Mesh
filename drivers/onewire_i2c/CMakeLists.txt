if(CONFIG_ONEWIRE_I2C)
    zephyr_include_directories(./)
	zephyr_library_sources(ds28e18.c)
endif()