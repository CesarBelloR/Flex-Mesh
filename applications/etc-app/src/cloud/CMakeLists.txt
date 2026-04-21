#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Copyright (c) 2023 EXACT Technology Corporation
#

target_include_directories(app PRIVATE .)
target_include_directories(app PRIVATE lwm2m)

add_subdirectory(cloud_codec)

target_sources_ifdef(CONFIG_AWS_IOT app
                     PRIVATE 
                     ${CMAKE_CURRENT_SOURCE_DIR}/aws_iot_integration.c)

if(CONFIG_LWM2M)
        target_sources(app
                       PRIVATE
                       ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/lwm2m_integration.c
                       ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/lwm2m_senml_cbor_time.c
        )
        add_subdirectory(lwm2m/exact_objects)
endif()

target_sources_ifdef(CONFIG_LWM2M_INTEGRATION_FIRMWARE_UPDATE_OBJ_SUPPORT app
                     PRIVATE 
                     ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/lwm2m_firmware.c)

