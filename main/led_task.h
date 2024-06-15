#ifndef _LED_TASK_H
#define _LED_TASK_H

#define LED_ON_STATE    0
#define LED_OFF_STATE   1
#define PULSE_PERIOD_MS ( 500 )

void led_task(void *pvParameter);

#endif