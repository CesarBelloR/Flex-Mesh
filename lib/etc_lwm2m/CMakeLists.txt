zephyr_library()

zephyr_library_sources_ifdef(CONFIG_EXACT_LWM2M_CLIENT_UTILS_SECURITY_OBJ_SUPPORT
                             etc_lwm2m_security.c)

zephyr_include_directories(./)                             