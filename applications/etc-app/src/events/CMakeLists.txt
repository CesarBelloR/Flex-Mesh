target_sources(app PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/app_event.c
		${CMAKE_CURRENT_SOURCE_DIR}/cloud_event.c
		${CMAKE_CURRENT_SOURCE_DIR}/lora_event.c
		${CMAKE_CURRENT_SOURCE_DIR}/data_event.c
		${CMAKE_CURRENT_SOURCE_DIR}/sensor_event.c
		${CMAKE_CURRENT_SOURCE_DIR}/modem_event.c
		${CMAKE_CURRENT_SOURCE_DIR}/ui_event.c
		${CMAKE_CURRENT_SOURCE_DIR}/util_event.c
		${CMAKE_CURRENT_SOURCE_DIR}/led_state_event.c
)
