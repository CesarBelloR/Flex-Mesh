cmake_minimum_required(VERSION 3.13.1)

set(CONFIG_APPLICATION_DEFINED_SYSCALL true)
list(APPEND mcuboot_OVERLAY_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/etc_ninab4_mcuboot.conf")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(etc_core)

target_sources(app PRIVATE src/main.c)
zephyr_cc_option(-Wfatal-errors)

zephyr_include_directories(src)
zephyr_include_directories(src/drivers)

target_sources_ifdef(CONFIG_BQ24195 app PRIVATE src/drivers/bq24195/bq24195.c)
target_sources_ifdef(CONFIG_PCF85263 app PRIVATE src/drivers/pcf85263/pcf85263.c)
target_sources_ifdef(CONFIG_DS2484 app PRIVATE src/drivers/onewire/ds2484.c)
target_sources_ifdef(CONFIG_DS18S20 app PRIVATE src/drivers/ds18s20/ds18s20.c)
target_sources(app PRIVATE src/drivers/adc/adc.c)
add_subdirectory(src/ui)
add_subdirectory(src/power)