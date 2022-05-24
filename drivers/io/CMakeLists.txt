if(CONFIG_ETC_IO)
    zephyr_include_directories(./)
	zephyr_library_sources(io.c)
endif()