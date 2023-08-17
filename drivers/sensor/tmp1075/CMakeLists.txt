# SPDX-License-Identifier: Apache-2.0
if(CONFIG_TMP1075)
    zephyr_include_directories(./)
	zephyr_library_sources(tmp1075.c)
endif()