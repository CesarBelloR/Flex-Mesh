zephyr_include_directories(include)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui.c)
target_sources_ifdef(
	CONFIG_UI_LED_USE_PWM
	app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/led_pwm.c
	)
