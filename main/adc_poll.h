#ifndef _ADC_POLL_H
    #define _ADC_POLL_H
    #define POLL_CHANNELS_NUM 4
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