if(CONFIG_MODEM_QUECTEL_BG95_M3)
	zephyr_library_include_directories(
		${ZEPHYR_BASE}/subsys/net/ip
		${ZEPHYR_BASE}/drivers/modem
	)
	zephyr_library_sources(quectel-bg95.c)
	zephyr_include_directories(./)
endif()

if(CONFIG_MODEM_QUECTEL_BG95_M3_MOCK)
	zephyr_library_include_directories(
		${ZEPHYR_BASE}/subsys/net/ip
	)
	zephyr_include_directories(./)
	zephyr_library_sources(quectel-bg95_mock.c)
endif()
