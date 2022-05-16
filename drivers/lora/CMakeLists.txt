if(CONFIG_LORA_SX1276)
	zephyr_library_include_directories(
		${ZEPHYR_BASE}/drivers/lora
	)
	zephyr_library_sources(sx1276.c)
endif()
