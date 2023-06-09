cmake_minimum_required(VERSION 3.13.1)

set(PM_STATIC_YML_FILE
  ${CMAKE_CURRENT_LIST_DIR}/boards/pm_static.yml
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(DEFINED ENV{VERSION})
project(app LANGUAGES C VERSION $ENV{VERSION})
else()
project(app LANGUAGES C VERSION 0.1.0)
endif()

set(PROJECT_VERSION_RC 7)

if(PROJECT_VERSION_RC)
  set(PROJECT_VERSION_SUFFIX "rc${PROJECT_VERSION_RC}")
else()
  set(PROJECT_VERSION_SUFFIX "")
endif()

if(PROJECT_VERSION_SUFFIX)
  set(PROJECT_VERSION ${PROJECT_VERSION}-${PROJECT_VERSION_SUFFIX})
endif()


configure_file(app_version.h.in ${CMAKE_BINARY_DIR}/app/include/app_version.h)

target_include_directories(app PRIVATE 
    ${CMAKE_BINARY_DIR}/app/include 
    src)

target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_MCUMGR_SMP_BT app PRIVATE src/bluetooth.c)

add_subdirectory(src/exact)
add_subdirectory(src/events)
add_subdirectory(src/modules)
add_subdirectory(src/data)
add_subdirectory(src/cloud)

zephyr_include_directories_ifdef(CONFIG_AWS_IOT_PROVISION_CERTIFICATES certs)