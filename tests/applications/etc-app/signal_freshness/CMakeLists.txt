#SPDX - License - Identifier : Apache - 2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(signal_freshness)

set(APP_SRC_DIR ../../../../applications/etc-app/src)

target_sources(app PRIVATE
	src/main.c
	${APP_SRC_DIR}/cloud/cloud_codec/data_codec_signal.c
)

# Mock include dirs come first so they shadow the real application headers
# (modem_api.h, events/modem_event.h), keeping the modem driver and the app
# event manager out of the build.
target_include_directories(app BEFORE PRIVATE
	src/mock
	${APP_SRC_DIR}/cloud/cloud_codec
)
