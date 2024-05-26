#ifndef _ADC_POLL_H
    #define _ADC_POLL_H
    #define POLL_CHANNELS_NUM 4
    typedef struct {
        unsigned int channel;
        int adc_raw;
        int voltage;
    } ADC_MESSAGE;

void adc_poll_task(void *);
#endif