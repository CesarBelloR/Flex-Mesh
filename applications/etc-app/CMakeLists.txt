cmake_minimum_required(VERSION 3.13.1)

set(PROJECT_VERSION_MAJOR 4)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 1)
set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})

# set(PROJECT_VERSION_DEV 1)
# set(PROJECT_VERSION_RC 3)
set(PROJECT_VERSION_CARRIER "" CACHE STRING "Carrier identifier suffix (e.g. sora, teal)")

if(PROJECT_VERSION_RC)
  set(PROJECT_VERSION_SUFFIX "rc${PROJECT_VERSION_RC}")
  set(PROJECT_VERSION_BUILD "${PROJECT_VERSION_RC}")
elseif(PROJECT_VERSION_DEV)
  set(PROJECT_VERSION_SUFFIX "dev${PROJECT_VERSION_DEV}")
  set(PROJECT_VERSION_BUILD "99")
else()
  if(PROJECT_VERSION_CARRIER)
    set(PROJECT_VERSION_SUFFIX "${PROJECT_VERSION_CARRIER}")
  else()
    set(PROJECT_VERSION_SUFFIX "")
  endif()
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

# When tools like VS Code nRF Connect pass -DCONF_FILE="prj.conf", Zephyr skips automatic
# inclusion of board fragments in boards/ (e.g. boards/etc_flex_nrf52840.conf and revision .conf).
# Unsetting CONF_FILE if it is just "prj.conf" allows Zephyr to discover all board/revision configs.
if(CONF_FILE STREQUAL "prj.conf")
  unset(CONF_FILE)
  unset(CONF_FILE CACHE)
endif()

# Similarly, if DTC_OVERLAY_FILE is set by the IDE or user, move it to EXTRA_DTC_OVERLAY_FILE
# so Zephyr's default board overlays in boards/ are still automatically discovered and applied.
if(DEFINED DTC_OVERLAY_FILE)
  list(APPEND EXTRA_DTC_OVERLAY_FILE ${DTC_OVERLAY_FILE})
  unset(DTC_OVERLAY_FILE)
  unset(DTC_OVERLAY_FILE CACHE)
endif()

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
