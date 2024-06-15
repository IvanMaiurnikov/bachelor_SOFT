#ifndef _ADC_POLL_H
    #define _ADC_POLL_H
    #define POLL_CHANNELS_NUM 4
    #define SAMPLES_PER_MEASURE 16
    #define MAX_CELL_VOLT (4.2)

    #define BAT_CHARGE_MODE 1
    #define BAT_HOLD_MODE 0
    #define BAT_DISCHARGE_MODE -1

    #define SLEEP_BITNUM 0
    #define WAKEUP_BITNUM 1

    #define ADC_VBAT_CHANNEL 0

    typedef struct {
        unsigned int channel;
        int adc_raw;
        float voltage;
    } ADC_MESSAGE;

    typedef struct 
    {
        float add;   //additive part of coefficient
        float mult;  //multiplicative
    } ADC_COEF;
    
void adc_poll_task(void *);
#endif