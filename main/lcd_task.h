#ifndef _LCD_TASK_H
#define _LCD_TASK_H
#define CONFIG_I2C_INTERFACE 1
#define LCD_STRING_LENGTH 16
void lcd_task(void *pvParameter);
#endif