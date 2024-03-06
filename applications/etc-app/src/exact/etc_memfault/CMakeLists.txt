#
# Copyright (c) 2023 EXACT Technology
#
#

target_include_directories(app PRIVATE .)
target_sources_ifdef(CONFIG_MEMFAULT_ETC app PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/etc_memfault.c
        ${CMAKE_CURRENT_SOURCE_DIR}/etc_memfault_metrics.c)

target_sources_ifndef(CONFIG_MEMFAULT_ETC app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/etc_memfault_dummy.c)