#
# Copyright (c) 2023 EXACT Technology Corporation
#

target_include_directories(app PRIVATE .)

target_sources(app PRIVATE
               ${CMAKE_CURRENT_SOURCE_DIR}/data_codec_ringbuffer.c
               ${CMAKE_CURRENT_SOURCE_DIR}/data_codec_signal.c)

target_sources_ifdef(CONFIG_LWM2M app
                     PRIVATE
                     ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/lwm2m_codec.c
                     ${CMAKE_CURRENT_SOURCE_DIR}/lwm2m/lwm2m_codec_helpers.c)