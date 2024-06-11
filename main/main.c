#include <stdio.h>
#include <stdlib.h>
#include <string.h> //Requires by memset
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
//#include "freertos/queue.h"
#include "wifi_task.h"
#include "adc_task.h"
#include "lcd_task.h"

static const char *TAG = "APP_MAIN"; // TAG for debug
extern TaskHandle_t TaskHandlerWifi;
extern TaskHandle_t TaskHandlerLCD;
void app_main()
{
    // Initialize NVS
    xTaskCreate(&adc_poll_task, "adc_task", 4096, NULL, 5, NULL);
    xTaskCreate(&lcd_task, "lcd_task",4096, NULL, 4, &TaskHandlerLCD);
    xTaskCreate(&wifi_task, "wifi_task", 4096, NULL, 5, &TaskHandlerWifi);
}