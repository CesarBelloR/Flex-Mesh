# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(etc_sensor_splitter_test)

set(APP_SRC_DIR ../../../../applications/etc-app/src)
set(EXACT_DIR ${ZEPHYR_ETC_COMMON_FIRMWARE_MODULE_DIR})

target_compile_options(app PRIVATE -Wno-error=double-promotion)

target_sources(app PRIVATE
  src/main.c
  src/mock/mock_deps.c
  src/mock/stub_devices.c
  ${APP_SRC_DIR}/exact/etc_sensor/etc_sensor.c
)

target_include_directories(app PRIVATE
  src
  ${APP_SRC_DIR}
  ${APP_SRC_DIR}/exact/etc_sensor
  ${APP_SRC_DIR}/exact/etc_devices
  ${APP_SRC_DIR}/exact/etc_battery
  ${APP_SRC_DIR}/exact/etc_calibration
  ${APP_SRC_DIR}/exact/etc_settings
  ${APP_SRC_DIR}/modules
  ${EXACT_DIR}/lib/adc
  ${EXACT_DIR}/drivers/sensor/tmp1826
)

zephyr_compile_definitions(CONFIG_ETC_SENSOR_LOG_LEVEL=3)
