# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(etc_sensor_calibration_load_test)

set(APP_SRC_DIR ../../../../applications/etc-app/src)

target_compile_options(app PRIVATE -Wno-error=double-promotion)

target_sources(app PRIVATE
  src/main.c
  src/mock/etc_device.c
  ${APP_SRC_DIR}/exact/etc_sensor/etc_sensor_calibration_load.c
)

target_include_directories(app PRIVATE
  src
  src/mock
  ${APP_SRC_DIR}
  ${APP_SRC_DIR}/exact/etc_sensor
  ${APP_SRC_DIR}/exact/etc_calibration
  ${APP_SRC_DIR}/exact/etc_devices
)

zephyr_compile_definitions(CONFIG_ETC_SENSOR_LOG_LEVEL=3)
