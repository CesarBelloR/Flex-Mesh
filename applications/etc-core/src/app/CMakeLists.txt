target_sources(app PRIVATE 
	${CMAKE_CURRENT_SOURCE_DIR}/etc_cape.c
	${CMAKE_CURRENT_SOURCE_DIR}/etc_lora.c
	${CMAKE_CURRENT_SOURCE_DIR}/etc_aws.c
	${CMAKE_CURRENT_SOURCE_DIR}/etc_setting.c
)

zephyr_include_directories(include)