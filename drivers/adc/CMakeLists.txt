if(CONFIG_ADC_MODULES)
    zephyr_include_directories(./)
	zephyr_library_sources(adc.c)
endif()