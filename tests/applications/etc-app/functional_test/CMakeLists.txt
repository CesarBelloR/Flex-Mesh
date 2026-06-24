#SPDX - License - Identifier : Apache - 2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(functional_test)

set(APP_SRC_DIR ../../../../applications/etc-app/src)

target_sources(app PRIVATE
	src/main.c
	src/mock/etc_settings_mock.c
	${APP_SRC_DIR}/exact/etc_functional_test/etc_functional_test.c
)

target_compile_options(app PRIVATE -Wno-error=double-promotion)

# Mock include dirs come first so they shadow the real application headers
# (etc_settings.h, data_codec.h, events/sensor_event.h).
target_include_directories(app BEFORE PRIVATE
	src/mock
	${APP_SRC_DIR}/exact/etc_functional_test
)

zephyr_compile_definitions(CONFIG_ETC_APP_LOG_LEVEL=3)
