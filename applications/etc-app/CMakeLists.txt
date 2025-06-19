cmake_minimum_required(VERSION 3.13.1)

set(PROJECT_VERSION_MAJOR 2)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

# set(PROJECT_VERSION_DEV 1)
set(PROJECT_VERSION_RC 6)

if(PROJECT_VERSION_RC)
  set(PROJECT_VERSION_SUFFIX "rc${PROJECT_VERSION_RC}")
  set(PROJECT_VERSION_BUILD "${PROJECT_VERSION_RC}")
elseif(PROJECT_VERSION_DEV)
  set(PROJECT_VERSION_SUFFIX "dev")
  set(PROJECT_VERSION_BUILD "99")
else()
  set(PROJECT_VERSION_SUFFIX "")
  set(PROJECT_VERSION_BUILD "0")
endif()

if(PROJECT_VERSION_SUFFIX)
  set(PROJECT_VERSION ${PROJECT_VERSION}-${PROJECT_VERSION_SUFFIX})
endif()

set(PM_STATIC_YML_FILE
  ${CMAKE_CURRENT_LIST_DIR}/boards/pm_static.yml
)

configure_file(app_version.h.in ${CMAKE_BINARY_DIR}/app/include/app_version.h)
configure_file(version.h.in ${CMAKE_BINARY_DIR}/app/config/mcu_version.conf)
list(APPEND OVERLAY_CONFIG ${CMAKE_BINARY_DIR}/app/config/mcu_version.conf)
# Include app_version.h for app and libraries
include_directories(app PRIVATE ${CMAKE_BINARY_DIR}/app/include)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

target_include_directories(app PRIVATE src)

target_sources(app PRIVATE src/main.c
  src/common.c)
target_sources_ifdef(CONFIG_MCUMGR_SMP_BT app PRIVATE src/bluetooth.c)

add_subdirectory(src/exact)
add_subdirectory(src/events)
add_subdirectory(src/modules)
add_subdirectory(src/data)
add_subdirectory(src/cloud)

zephyr_include_directories_ifdef(CONFIG_MEMFAULT configuration/memfault)
zephyr_include_directories_ifdef(CONFIG_AWS_IOT_PROVISION_CERTIFICATES certs)

if(CONFIG_NRFX_TWIM AND CONFIG_I2C_LOG_LEVEL_DBG)
  zephyr_compile_definitions(NRFX_TWIM_CONFIG_LOG_ENABLED=1)
  # zephyr_compile_definitions(NRFX_SPIM_CONFIG_LOG_ENABLED=1)
endif()
