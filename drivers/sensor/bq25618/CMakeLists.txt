if(CONFIG_BQ25618)
    zephyr_include_directories(./)
    zephyr_library_sources(bq25618.c)
endif()