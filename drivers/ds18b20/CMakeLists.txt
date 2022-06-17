if(CONFIG_DS18B20)
    zephyr_include_directories(./)
	zephyr_library_sources(ds18b20.c onewire.c)
endif()