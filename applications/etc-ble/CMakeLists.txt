cmake_minimum_required(VERSION 3.13.1)

set(CONFIG_APPLICATION_DEFINED_SYSCALL true)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(etc-ble)

target_sources(app PRIVATE src/main.c
    src/flex_ble.c
)
zephyr_cc_option(-Wfatal-errors)

zephyr_include_directories(src)
