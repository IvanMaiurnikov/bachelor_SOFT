#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "ssd1306.h"
#include "font8x8_basic.h"
#include "lcd_task.h"
#include "esp_log.h"
#include "adc_task.h"
#define tag "SSD1306"
//-----------------------------------------------------------
extern ADC_MESSAGE adc_msg[POLL_CHANNELS_NUM];
//-----------------------------------------------------------
void lcd_task(void *pvParameter) {
    SSD1306_t dev;
	char lcd_buf[17]; //16 + 1 byte for trailing zero
    int top;
    ESP_LOGI(tag, "INTERFACE is i2c");
	ESP_LOGI(tag, "CONFIG_SDA_GPIO=%d",CONFIG_SDA_GPIO);
	ESP_LOGI(tag, "CONFIG_SCL_GPIO=%d",CONFIG_SCL_GPIO);
	ESP_LOGI(tag, "CONFIG_RESET_GPIO=%d",CONFIG_RESET_GPIO);
	i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
    ESP_LOGI(tag, "Panel is 128x32");
	ssd1306_init(&dev, 128, 32);
    top = 1;
	memset((void *)lcd_buf, 0, sizeof(lcd_buf));
	ssd1306_clear_screen(&dev, false);
    while(1){
		sprintf(lcd_buf, "V1:%.2f V2: %.2f", adc_msg[1].voltage, adc_msg[2].voltage);
		ssd1306_display_text(&dev, 0, lcd_buf, strlen(lcd_buf), false);
		sprintf(lcd_buf, "V3:%.2f V4: %.2f", adc_msg[3].voltage, adc_msg[4].voltage);
		ssd1306_display_text(&dev, 1, lcd_buf, strlen(lcd_buf), false);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
