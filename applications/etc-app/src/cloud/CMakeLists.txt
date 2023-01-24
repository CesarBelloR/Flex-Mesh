#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

target_include_directories(app PRIVATE .)

target_sources_ifdef(CONFIG_AWS_IOT app
                     PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/aws_iot_integration.c)

# target_sources_ifdef(CONFIG_LWM2M_INTEGRATION app
#                      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m_integration/lwm2m_integration.c)

# target_sources_ifdef(CONFIG_LWM2M_INTEGRATION app
#                      PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m_integration/configuration_object.c)
