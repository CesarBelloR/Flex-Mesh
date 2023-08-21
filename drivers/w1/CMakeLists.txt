# SPDX-License-Identifier: Apache-2.0
if(CONFIG_EXACT_DS2484)
    zephyr_include_directories(./)
    zephyr_library_include_directories(
        ${ZEPHYR_BASE}/drivers/w1
    )
	zephyr_library_sources(exact_ds2484.c)
endif()