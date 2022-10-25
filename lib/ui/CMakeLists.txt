zephyr_library()
zephyr_include_directories(./include)
zephyr_library_include_directories(
	./include
)
zephyr_library_sources(ui.c)
zephyr_library_sources_ifdef(CONFIG_UI_LED_USE_PWM led_pwm.c)