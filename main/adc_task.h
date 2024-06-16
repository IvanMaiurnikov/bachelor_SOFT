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
        uint16_t channel,          //ADC channel
                 cycles,           //Charge/discharge cycles conuner
                 capacity_percent;
        int16_t  adc_raw,          //raw value of ADC
                 bat_mode;         //Battery running mode: discharge=-1, hold=0, charge=1
        float voltage;             //Voltage according to calibration value

    } ADC_MESSAGE;

    typedef struct 
    {
        float add;   //additive part of coefficient
        float mult;  //multiplicative
    } ADC_COEF;
    
void adc_poll_task(void *);
#endif