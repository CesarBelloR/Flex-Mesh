cmake_minimum_required(VERSION 3.13.1)

set(CONFIG_APPLICATION_DEFINED_SYSCALL true)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(cbor)

target_sources(app PRIVATE src/main.c
)
zephyr_cc_option(-Wfatal-errors)
zephyr_cc_option(-Wmissing-braces)