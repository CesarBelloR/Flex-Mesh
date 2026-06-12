# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(etc_reclaim)

set(APP_SRC_DIR ../../../../../applications/etc-app/src)

target_sources(app PRIVATE
  src/main.c
  ${APP_SRC_DIR}/cloud/lwm2m/exact_objects/etc_reclaim_obj_48934.c
)

target_include_directories(app PRIVATE
    ${ZEPHYR_BASE}/include
    ${ZEPHYR_BASE}/subsys/net/lib/lwm2m
    ${APP_SRC_DIR}/cloud/lwm2m/exact_objects
)
