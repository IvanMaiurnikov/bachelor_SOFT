#include <stdio.h>
#include <stdlib.h>
#include <string.h> //Requires by memset
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include "freertos/queue.h"
#include "wifi_task.h"
#include "adc_task.h"
#include "lcd_task.h"
#include "led_task.h"

static const char *TAG = "APP_MAIN"; // TAG for debug
extern TaskHandle_t TaskHandlerLCD;
extern TaskHandle_t TaskHandlerLED;
void app_main() {
    // Initialize NVS
    xTaskCreate(&adc_poll_task, "adc_task", 4096, NULL, 5, NULL);
    xTaskCreate(&lcd_task, "lcd_task",4096, NULL, 4, &TaskHandlerLCD);
    //xTaskCreate(&led_task, "led_task",1024, NULL, 6, &TaskHandlerLED);
}