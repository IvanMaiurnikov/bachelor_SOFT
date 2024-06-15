set(srcs
	 "main.c"
     "wifi_task.c"
     "adc_task.c"
     "led_task.c"
    )
idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ".")

spiffs_create_partition_image(storage ../data FLASH_IN_PROJECT)
