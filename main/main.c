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


#define GPIO_LED GPIO_NUM_22
#define LED_ON_STATE 0
#define LED_OFF_STATE 1
extern ADC_MESSAGE adc_msg[POLL_CHANNELS_NUM];
//TaskHandle_t TaskHandlerLED;

void app_main() {
    /*
    LED_STATE_ENUM led_state = LED_OFF;
    uint32_t on_state_tout = 0;
    gpio_reset_pin(GPIO_LED);
    gpio_set_direction(GPIO_LED, GPIO_MODE_OUTPUT); // Set the GPIO as a push/pull output
    gpio_set_pull_mode(GPIO_LED, GPIO_PULLUP_ENABLE);
    */

    // Initialize NVS
    xTaskCreate(&adc_poll_task, "adc_task", 4096, NULL, 5, NULL);
    xTaskCreate(&lcd_task, "lcd_task",4096, NULL, 6, &TaskHandlerLCD); //
    xTaskCreate(&led_task, "led_task",4096, NULL, 7, &TaskHandlerLED);
   /* 
    while(1){
        on_state_tout = volt_to_pulse(adc_msg[0].voltage, PULSE_PERIOD_MS);
        //on_state_tout = volt_to_pulse(3.85, PULSE_PERIOD_MS);
        if (on_state_tout < 5) on_state_tout = 5;
        ESP_LOGI(TAG, "On state = %lu", on_state_tout);
        if (led_state==LED_OFF){
            ESP_LOGI(TAG, "On state");
            led_state = LED_ON;
            gpio_set_level(GPIO_LED, 0);
            vTaskDelay(pdMS_TO_TICKS(on_state_tout));
        } else {
                ESP_LOGI(TAG, "OFF state");
                led_state = LED_OFF;
                if (on_state_tout != PULSE_PERIOD_MS){
                    gpio_set_level(GPIO_LED, 1);
                }
                vTaskDelay(pdMS_TO_TICKS(PULSE_PERIOD_MS - on_state_tout));
            
        }
    }
    */
}