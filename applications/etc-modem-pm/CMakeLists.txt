cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

if(DEFINED ENV{VERSION})
project(app LANGUAGES C VERSION $ENV{VERSION})
else()
project(app LANGUAGES C VERSION 1.0.0)
endif()

target_sources(app PRIVATE src/main.c)