cmake_minimum_required(VERSION 3.13.1)

set(CONFIG_APPLICATION_DEFINED_SYSCALL true)
list(APPEND mcuboot_OVERLAY_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/etc_mcuboot.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(etc_core)

target_sources(app PRIVATE src/main.c)
zephyr_cc_option(-Wfatal-errors)

add_subdirectory(src/app)
include_directories(
  src/app
)