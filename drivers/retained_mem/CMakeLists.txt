
zephyr_syscall_header(${ZEPHYR_BASE}/include/zephyr/drivers/retained_mem.h)

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_RETAINED_MEM_MOCK_RAM retained_mem_mock.c)