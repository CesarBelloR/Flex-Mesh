target_sources(app PRIVATE
	       ${CMAKE_CURRENT_SOURCE_DIR}/app_module.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/cloud_module.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/data_module.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/sensor_module.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/ui_module.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/modules_common.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/modem_module.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/util_module.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/ble_module.c
)

target_sources_ifdef(CONFIG_ETC_LORA_MODULE app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lora_module.c)
target_sources_ifdef(CONFIG_DEBUG_MODULE app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/debug_module.c)
