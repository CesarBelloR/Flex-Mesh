#SPDX - License - Identifier : Apache - 2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tx_interval)

string(TIMESTAMP UNIX_TIME "%s")
message("Compile this project at : ${UNIX_TIME}")
if(DEFINED ENV{VERSION})
project(app LANGUAGES C VERSION $ENV{VERSION})
else()
project(app LANGUAGES C VERSION 0.0.0)
endif()

configure_file(app_version.h.in ${CMAKE_BINARY_DIR}/app/include/app_version.h)

FILE(GLOB app_sources src/*.c
                      src/mock/*.c
                      src/mock/cloud/cloud_codec/lwm2m/lwm2m_codec_helpers.c
                      src/mock/zephyr/drivers/*.c)

target_sources(app PRIVATE ${app_sources})

set(APP_SRC_DIR ../../../../applications/etc-app/src)
set(DRIVER_SRC_DIR ../../../../drivers)

target_sources(app PRIVATE 
                ${APP_SRC_DIR}/modules/app_module_helper.c
                ${APP_SRC_DIR}/exact/etc_devices/etc_device.c
                ${APP_SRC_DIR}/exact/etc_devices/etc_device_record.c
                ${APP_SRC_DIR}/exact/etc_settings/etc_settings.c
                ${APP_SRC_DIR}/exact/etc_memfault/etc_memfault_dummy.c
                ${APP_SRC_DIR}/exact/etc_util/etc_util.c
)

set(INCLUDE_DIRS
        src
        src/mock
        src/mock/zephyr/drivers
        ${APP_SRC_DIR}/exact/etc_devices
        ${APP_SRC_DIR}/exact/etc_memfault
        ${APP_SRC_DIR}/exact/etc_settings
        ${APP_SRC_DIR}/exact/etc_sensor
        ${APP_SRC_DIR}/exact/etc_util
        ${APP_SRC_DIR}/exact/etc_functional_test
        ${APP_SRC_DIR}/modules
        ${APP_SRC_DIR}
)

list(REMOVE_ITEM INCLUDE_DIRS ${APP_SRC_DIR}/cloud)

target_include_directories(app PRIVATE ${INCLUDE_DIRS}
        ${CMAKE_BINARY_DIR}/app/include
)

zephyr_compile_definitions_ifdef(CONFIG_BOARD_NATIVE_SIM CONFIG_BOARD_VERSION="0.0.0")
zephyr_compile_definitions(CONFIG_LWM2M_SECURITY_KEY_SIZE=33)
zephyr_compile_definitions(CONFIG_SERIAL_NUMBER_TYPE=1)
zephyr_compile_definitions(CONFIG_SERIAL_NUMBER_DEFAULT_VALUE=10000000)
zephyr_compile_definitions(CONFIG_LWM2M_INTEGRATION_PSK="70475440693636213646256D7744")
zephyr_compile_definitions(CONFIG_MODEM_MODULE_GNSS_TIMEOUT_S=60)
zephyr_compile_definitions(CONFIG_MODEM_MODULE_MAX_CONNECTION_TIME_S=60)
zephyr_compile_definitions(IMAGE_HASH_LEN=32)
zephyr_compile_definitions(CONFIG_ETC_APP_LOG_LEVEL=3)
zephyr_compile_definitions(CONFIG_ETC_SETTINGS_LOG_LEVEL=3)
zephyr_compile_definitions(CONFIG_ETC_DEVICE_RELAY_MAX_RECORD_HISTORY=64)
zephyr_compile_definitions(CONFIG_ETC_DEVICE_SAVE_RECORD_SEC=1)
zephyr_compile_definitions(CONFIG_LWM2M_ETC_RELAY_OBJ_DATA_SIZE=512)