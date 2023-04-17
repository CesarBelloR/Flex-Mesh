target_include_directories(app PRIVATE 
        ${ZEPHYR_BASE}/subsys/mgmt/mcumgr/
)
target_include_directories(app PRIVATE .)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/etc_mgmt.c)
