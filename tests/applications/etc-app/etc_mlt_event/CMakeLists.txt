#SPDX - License - Identifier : Apache - 2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(etc_device_record_test)

FILE(GLOB app_sources src/*.c
                      src/mock/*.c
                      src/mock/zephyr/drivers/*.c)
target_sources(app PRIVATE ${app_sources})
set(APP_SRC_DIR ../../../../applications/etc-app/src)
set(MEMFAULT_SDK_ROOT $ENV{ZEPHYR_BASE}/../modules/lib/memfault-firmware-sdk)

target_compile_options(app PRIVATE -Wno-error=double-promotion)

target_sources(app PRIVATE 
                ${APP_SRC_DIR}/exact/etc_devices/etc_device.c
                ${APP_SRC_DIR}/exact/etc_devices/etc_device_record.c
                ${APP_SRC_DIR}/exact/etc_memfault/etc_memfault_dummy.c
                ${APP_SRC_DIR}/exact/etc_memfault/etc_memfault_event.c
                ${APP_SRC_DIR}/exact/etc_util/etc_util.c
                ${MEMFAULT_SDK_ROOT}/components/util/src/memfault_circular_buffer.c
                ${MEMFAULT_SDK_ROOT}/components/util/src/memfault_minimal_cbor.c
                ${MEMFAULT_SDK_ROOT}/components/core/src/memfault_batched_events.c
)

target_include_directories(app PRIVATE
                            src
                            src/mock
                            src/mock/zephyr/drivers
                            ${APP_SRC_DIR}/exact/etc_devices
                            ${APP_SRC_DIR}/exact/etc_memfault
                            ${APP_SRC_DIR}/exact/etc_settings
                            ${APP_SRC_DIR}/exact/etc_sensor
                            ${APP_SRC_DIR}/exact/etc_util
                            ${APP_SRC_DIR}
                            ${MEMFAULT_SDK_ROOT}/components/include
)

zephyr_compile_definitions(IMAGE_HASH_LEN=32)
zephyr_compile_definitions(CONFIG_ETC_APP_LOG_LEVEL=4)
zephyr_compile_definitions(CONFIG_ETC_DEVICE_RELAY_MAX_RECORD_HISTORY=64)
zephyr_compile_definitions(CONFIG_ETC_DEVICE_SAVE_RECORD_SEC=1)
zephyr_compile_definitions(CONFIG_MEMFAULT_ETC_EVENT_NOINIT_SIZE=2048)