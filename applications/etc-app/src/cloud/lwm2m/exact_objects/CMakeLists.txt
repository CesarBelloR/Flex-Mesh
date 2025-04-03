#
# Copyright (c) 2021 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
# Copyright (c) 2023 EXACT Technology Corporation
#

target_include_directories(app PRIVATE .)

target_sources(app
               PRIVATE 
               ${CMAKE_CURRENT_SOURCE_DIR}/etc_cfg_obj_48931.c
               ${CMAKE_CURRENT_SOURCE_DIR}/etc_reclaim_obj_48934.c
               ${CMAKE_CURRENT_SOURCE_DIR}/etc_relay_obj_48935.c
               ${CMAKE_CURRENT_SOURCE_DIR}/etc_humid_obj_48936.c
               ${CMAKE_CURRENT_SOURCE_DIR}/etc_functional_test_obj_48937.c
               ${CMAKE_CURRENT_SOURCE_DIR}/etc_location_obj_48938.c
               ${CMAKE_CURRENT_SOURCE_DIR}/etc_calibration_obj_48950.c
               ${CMAKE_CURRENT_SOURCE_DIR}/etc_calibration_status_obj_48939.c
)
