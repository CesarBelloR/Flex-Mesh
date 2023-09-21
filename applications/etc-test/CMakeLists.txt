cmake_minimum_required(VERSION 3.13.1)

set(PM_STATIC_YML_FILE
  ${CMAKE_CURRENT_LIST_DIR}/boards/pm_static.yml
)

set(CONFIG_APPLICATION_DEFINED_SYSCALL true)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(etc_test)

target_sources(app PRIVATE src/main.c
    src/etc_test.c
    src/etc_cape.c
    src/modem.c
    src/gnss.c
    src/bluetooth.c
)
zephyr_cc_option(-Wfatal-errors)

zephyr_include_directories(src)

add_subdirectory(src/modules)
add_subdirectory(src/etc_devices)