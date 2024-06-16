#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_task.h"
#include "adc_task.h"
//#include "lcd_task.h"
#include "led_task.h"

static const char *TAG = "APP_MAIN"; // TAG for debug
//extern TaskHandle_t TaskHandlerLCD;
extern TaskHandle_t TaskHandlerLED;
extern TaskHandle_t TaskHandlerADC;
extern TaskHandle_t wifi_handler;

#define GPIO_LED GPIO_NUM_22
#define LED_ON_STATE 0
#define LED_OFF_STATE 1

void app_main() {
    ESP_LOGI(TAG, "Starting tasks");
    xTaskCreate(&adc_poll_task, "adc_task", 4096, NULL, 5, NULL);
    //xTaskCreate(&lcd_task, "lcd_task",4096, NULL, 6, &TaskHandlerLCD); //
    xTaskCreate(&wifi_task, "wifi_task",4096, NULL, 7, &wifi_handler);
    xTaskCreate(&led_task, "led_task",4096, NULL, 6, &TaskHandlerLED);
}