# SPDX-License-Identifier: Apache-2.0
if(CONFIG_SHT31)
    zephyr_library()
    zephyr_library_sources(sht31.c)
endif()
