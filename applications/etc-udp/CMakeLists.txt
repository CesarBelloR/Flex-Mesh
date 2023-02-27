cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(DEFINED ENV{VERSION})
project(app LANGUAGES C VERSION $ENV{VERSION})
else()
project(app LANGUAGES C VERSION 0.0.0)
endif()

configure_file(app_version.h.in ${CMAKE_BINARY_DIR}/app/include/app_version.h)

target_include_directories(app PRIVATE 
    ${CMAKE_BINARY_DIR}/app/include 
    src)

target_sources(app PRIVATE src/main.c)