#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Copyright (c) 2023 EXACT Technology Corporation
#

target_include_directories(app PRIVATE .)
target_include_directories(app PRIVATE lwm2m)
target_include_directories(app PRIVATE lwm2m/exact_objects)

add_subdirectory(cloud_codec)

target_sources_ifdef(CONFIG_AWS_IOT app
                     PRIVATE 
                     ${CMAKE_CURRENT_SOURCE_DIR}/aws_iot_integration.c)

target_sources_ifdef(CONFIG_LWM2M app
                     PRIVATE 
                     ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/lwm2m_integration.c
                     ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/exact_objects/etc_cfg_obj_48931.c
                     ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/exact_objects/etc_temp_obj_48932.c
                     ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/exact_objects/etc_info_obj_48933.c
                     ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/exact_objects/etc_reclaim_obj_48934.c
                     ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/exact_objects/etc_relay_obj_48935.c
                     ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/exact_objects/etc_humid_obj_48936.c
)

target_sources_ifdef(CONFIG_LWM2M_INTEGRATION_FIRMWARE_UPDATE_OBJ_SUPPORT app
                     PRIVATE 
                     ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/lwm2m_firmware.c)

