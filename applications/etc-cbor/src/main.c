#include <stdio.h>
#include <zcbor_decode.h>
#include <zcbor_encode.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(cbor);

#define MAX_STATE 24

uint8_t zcbor_buffer[1024];
zcbor_state_t state[MAX_STATE];
zcbor_state_t *p_state_encode = &state[0];

// {
//    "relay_id": 1234,
//    "timestamp": 1636384512,
//    "logger": [{
//        "logger_id": 123,
//        "time": 1687901671,
//        "packet": 1,
//        "ports": {
//            "0": [{
//                    "measurement": 0,
//                    "type": 0,
//                    "unit": "Cel",
//                    "value": 54.2
//                }
//            ],
//            "1": [{
//                    "measurement": 0,
//                    "type": 0,
//                    "unit": "Cel",
//                    "value": 54.2
//                },
//                {
//                    "measurement": 1,
//                    "type": 1,
//                    "unit": "%RH",
//                    "value": 54.2
//                }
//            ],
//            "2": [{
//                    "measurement": 0,
//                    "type": 0,
//                    "unit": "Cel",
//                    "value": 54.2
//                }
//            ],
//            "3": [{
//                    "measurement": 0,
//                    "type": 0,
//                    "unit": "Cel",
//                    "value": 54.2
//                }
//            ],
//            "4": [{
//                    "measurement": 0,
//                    "type": 0,
//                    "unit": "Cel",
//                    "value": 54.2
//                }
//            ]
//        }
//    }]
// }

// {
//    "1": 1234,
//    "2": 1636384512,
//    "3": [{
//        "1": 123,
//        "2": 1687901671,
//        "3": 1,
//        "4": {
//            "0": [{
//                    "1": 0,
//                    "2": 0,
//                    "3": "Cel",
//                    "4": 54.2
//                }
//            ],
//            "1": [{
//                    "1": 0,
//                    "2": 0,
//                    "3": "Cel",
//                    "4": 54.2
//                },
//                {
//                    "1": 1,
//                    "2": 1,
//                    "3": "%RH",
//                    "4": 54.2
//                }
//            ],
//            "2": [{
//                    "1": 0,
//                    "2": 0,
//                    "3": "Cel",
//                    "4": 54.2
//                }
//            ],
//            "3": [{
//                    "1": 0,
//                    "2": 0,
//                    "3": "Cel",
//                    "4": 54.2
//                }
//            ],
//            "4": [{
//                    "1": 0,
//                    "2": 0,
//                    "3": "Cel",
//                    "4": 54.2
//                }
//            ]
//        }
//    }]
// }

void main(void)
{
	zcbor_new_encode_state(p_state_encode, MAX_STATE, zcbor_buffer, sizeof(zcbor_buffer), 0);
	
	zcbor_map_start_encode(p_state_encode, 0);

		zcbor_tstr_put_lit(p_state_encode, "1");
		zcbor_uint64_put(p_state_encode, 1234);
		
		zcbor_tstr_put_lit(p_state_encode, "2");
		zcbor_uint64_put(p_state_encode, 1636384512);

		zcbor_tstr_put_lit(p_state_encode, "3");
		zcbor_list_start_encode(p_state_encode, 0);
			zcbor_map_start_encode(p_state_encode, 0);
				zcbor_tstr_put_lit(p_state_encode, "1");
				zcbor_uint64_put(p_state_encode, 123);

				zcbor_tstr_put_lit(p_state_encode, "2");
				zcbor_uint64_put(p_state_encode, 1687901671);

				zcbor_tstr_put_lit(p_state_encode, "3");
				zcbor_uint64_put(p_state_encode, 1);	

				zcbor_tstr_put_lit(p_state_encode, "4");
				zcbor_list_start_encode(p_state_encode, 0);
					zcbor_map_start_encode(p_state_encode, 0);

						zcbor_tstr_put_lit(p_state_encode, "0");
							zcbor_list_start_encode(p_state_encode, 0);
								zcbor_map_start_encode(p_state_encode, 0);

									zcbor_tstr_put_lit(p_state_encode, "1");
									zcbor_uint32_put(p_state_encode, 0);
									
									zcbor_tstr_put_lit(p_state_encode, "2");
									zcbor_uint32_put(p_state_encode, 0);

									zcbor_tstr_put_lit(p_state_encode, "3");
									zcbor_tstr_put_term(p_state_encode, "Cel");

									zcbor_tstr_put_lit(p_state_encode, "4");
									zcbor_float32_put(p_state_encode, 54.2);

								zcbor_map_end_encode(p_state_encode, 0);
							zcbor_list_end_encode(p_state_encode, 0);
						zcbor_tstr_put_lit(p_state_encode, "1");
							zcbor_list_start_encode(p_state_encode, 0);
								zcbor_map_start_encode(p_state_encode, 0);

									zcbor_tstr_put_lit(p_state_encode, "1");
									zcbor_uint32_put(p_state_encode, 0);
									
									zcbor_tstr_put_lit(p_state_encode, "2");
									zcbor_uint32_put(p_state_encode, 0);

									zcbor_tstr_put_lit(p_state_encode, "3");
									zcbor_tstr_put_term(p_state_encode, "Cel");

									zcbor_tstr_put_lit(p_state_encode, "4");
									zcbor_float32_put(p_state_encode, 54.2);

								zcbor_map_end_encode(p_state_encode, 0);
									zcbor_map_start_encode(p_state_encode, 0);

									zcbor_tstr_put_lit(p_state_encode, "1");
									zcbor_uint32_put(p_state_encode, 1);
									
									zcbor_tstr_put_lit(p_state_encode, "2");
									zcbor_uint32_put(p_state_encode, 1);

									zcbor_tstr_put_lit(p_state_encode, "3");
									zcbor_tstr_put_term(p_state_encode, "%%RH");

									zcbor_tstr_put_lit(p_state_encode, "4");
									zcbor_float32_put(p_state_encode, 89.2);

								zcbor_map_end_encode(p_state_encode, 0);
							zcbor_list_end_encode(p_state_encode, 0);
					zcbor_map_end_encode(p_state_encode, 0);
				zcbor_list_end_encode(p_state_encode, 4);
			zcbor_map_end_encode(p_state_encode, 0);
		zcbor_list_end_encode(p_state_encode, 4);
	zcbor_map_end_encode(p_state_encode, 0);
	LOG_HEXDUMP_INF(zcbor_buffer, p_state_encode->payload_mut - zcbor_buffer, "Payload");
}