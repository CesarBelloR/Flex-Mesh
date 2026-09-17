# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(threshold_eval_test)

set(APP_SRC_DIR ../../../../applications/etc-app/src)

target_sources(app PRIVATE src/main.c ${APP_SRC_DIR}/modules/threshold_eval.c)

# etc_sensor.h, pulled in by etc_device.h, promotes floats in a static inline.
target_compile_options(app PRIVATE -Wno-error=double-promotion)

target_include_directories(app PRIVATE
        src
        ${APP_SRC_DIR}
        ${APP_SRC_DIR}/modules
        ${APP_SRC_DIR}/exact/etc_devices
        ${APP_SRC_DIR}/exact/etc_sensor
        ${APP_SRC_DIR}/exact/etc_calibration
        ${APP_SRC_DIR}/exact/etc_util
)

zephyr_compile_definitions(CONFIG_ETC_APP_THRESHOLD_MIN_REPORT_INTERVAL_S=900)
