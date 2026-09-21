# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(etc_sensor_sim_test)

set(APP_SRC_DIR ../../../../applications/etc-app/src)

target_sources(app PRIVATE src/main.c ${APP_SRC_DIR}/exact/etc_sensor/etc_sensor_sim.c)

target_include_directories(app PRIVATE
        src
        ${APP_SRC_DIR}
        ${APP_SRC_DIR}/exact/etc_sensor
)

# The real symbols live under "menuconfig ETC_SENSOR" in the app Kconfig, which
# cannot be sourced for native_sim, and etc_sensor.c owns the log module.
zephyr_compile_definitions(CONFIG_ETC_SENSOR_SIM_SHELL=1)
zephyr_compile_definitions(CONFIG_ETC_SENSOR_LOG_LEVEL=3)
