target_include_directories(app PRIVATE .)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/etc_ble.c
	${CMAKE_CURRENT_SOURCE_DIR}/ble_helpers.c
)