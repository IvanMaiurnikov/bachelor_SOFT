#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "adc_task.h"
#include "led_task.h"
#define GPIO_LED GPIO_NUM_22

static const char TAG[]="LED_TASK";
extern ADC_MESSAGE adc_msg;

TaskHandle_t TaskHandlerLED = NULL;
static uint32_t volt_to_pulse(float volt, uint32_t period){
    uint32_t pulse_width = 0;
    /*
    100%----4.20V
    90%-----4.06V
    80%-----3.98V
    70%-----3.92V
    60%-----3.87V
    50%-----3.82V
    40%-----3.79V
    30%-----3.77V
    20%-----3.74V
    10%-----3.68V
    5%------3.45V
    0%------3.00V
    */
   
   if(volt > 4.15) pulse_width = 100;
   else if(volt >= 4.06) pulse_width = 90;
   else if(volt >= 3.98) pulse_width = 80;
   else if(volt >= 3.92) pulse_width = 70;
   else if(volt >= 3.87) pulse_width = 60;
   else if(volt >= 3.82) pulse_width = 50;
   else if(volt >= 3.79) pulse_width = 40;
   else if(volt >= 3.77) pulse_width = 30;
   else if(volt >= 3.74) pulse_width = 20;
   else if(volt >= 3.68) pulse_width = 10;
   else pulse_width = 5;
   return (pulse_width * period)/100;
}

void led_task(void *pvParameter){
    TickType_t on_state_tout = 0;
    gpio_reset_pin(GPIO_LED);
    gpio_set_direction(GPIO_LED, GPIO_MODE_OUTPUT); ///* Set the GPIO as a push/pull output */
    gpio_set_pull_mode(GPIO_LED, GPIO_PULLUP_ENABLE);
    ESP_LOGI(TAG, "Starting LED task");
    while(1){
        on_state_tout = volt_to_pulse(adc_msg.voltage, PULSE_PERIOD_MS);
        if (on_state_tout < 5) on_state_tout = 5;
        //ESP_LOGI(TAG, "LED on state, ms: %lu", on_state_tout);
        gpio_set_level(GPIO_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(on_state_tout));
        //ESP_LOGI(TAG, "LED off");
        gpio_set_level(GPIO_LED, 1);
        if(on_state_tout >= PULSE_PERIOD_MS) on_state_tout = PULSE_PERIOD_MS - 5;
        vTaskDelay(pdMS_TO_TICKS(PULSE_PERIOD_MS - on_state_tout));
    }
}