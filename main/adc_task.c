#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "adc_task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#include "wifi_task.h"

#define ADC_POLL_ATTEN           ADC_ATTEN_DB_12

const static char *TAG = "ADC_POLLING";

const static ADC_COEF ADC_CHANNEL_CONV_COEF = {0.0, 2.0};

ADC_MESSAGE adc_msg = {
    .adc_raw = 0,
    .channel = 0,
    .voltage = 0,
    .cycles = 0,
    .bat_mode = BAT_HOLD_MODE
};
extern TaskHandle_t wifi_handler;
extern TaskHandle_t TaskHandlerLED;
TaskHandle_t TaskHandlerADC = NULL;
static int adc_sleep = 0;

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool adc_poll_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

static void adc_poll_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}

static int16_t adc_wakeup_subtasks()
{
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT0);
    //xTaskNotify(wifi_handler, 1 << WAKEUP_BITNUM, eSetBits);
    xTaskCreate(&wifi_task, "wifi_task",4096, NULL, 7, &wifi_handler);
    xTaskNotify(TaskHandlerLED, 1 << WAKEUP_BITNUM, eSetBits);
    return 0;
}

static int16_t adc_sleep_subtasks(){
    xTaskNotify(wifi_handler, 1 << SLEEP_BITNUM, eSetBits);
    xTaskNotify(TaskHandlerLED, 1 << SLEEP_BITNUM, eSetBits);
    return 1;
}

static uint16_t volt_to_capacity_percent(float volt){
    uint16_t capacity_percent = 0;
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
    0%------3.30V
    */
   
   if(volt > 4.18) capacity_percent = 100;
   else if(volt >= 4.06) capacity_percent = 90;
   else if(volt >= 3.98) capacity_percent = 80;
   else if(volt >= 3.92) capacity_percent = 70;
   else if(volt >= 3.87) capacity_percent = 60;
   else if(volt >= 3.82) capacity_percent = 50;
   else if(volt >= 3.79) capacity_percent = 40;
   else if(volt >= 3.77) capacity_percent = 30;
   else if(volt >= 3.74) capacity_percent = 20;
   else if(volt >= 3.68) capacity_percent = 10;
   else if(volt >= 3.45) capacity_percent = 5;
   else if(volt >= 3.31) capacity_percent = 1;
   else capacity_percent = 0;
   return capacity_percent;
}

void adc_poll_task(void *pvParameter) {
    esp_sleep_wakeup_cause_t wakeup_reason;
    float prev_total_volt = 0.0;
    uint16_t sec_counter = 0,
             charge_discharge_cycles = 0;
    int16_t  bat_running_mode = BAT_CHARGE_MODE;          //battery running mode: 1 - charge, 0 - discharge 
    int      mv, 
             mv_sum;
        //-------------ADC1 Init---------------//
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_POLL_ATTEN,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_VBAT_CHANNEL, &config));

    //-------------ADC1 Calibration Init---------------//
    adc_cali_handle_t adc1_cali_handle = NULL;
    bool do_calibration1 = false;
    do_calibration1 = adc_poll_calibration_init(ADC_UNIT_1, ADC_VBAT_CHANNEL, ADC_POLL_ATTEN, &adc1_cali_handle);
    while(1){
        //check sleeping mode
        if (adc_sleep){
            wakeup_reason = esp_sleep_get_wakeup_cause();
            switch (wakeup_reason){
                case ESP_SLEEP_WAKEUP_EXT0:
                     ESP_LOGI(TAG, "Wake up by button");
                     adc_sleep = adc_wakeup_subtasks();
                     break;
                case ESP_SLEEP_WAKEUP_TIMER:
                     ESP_LOGI(TAG, "Wake up by timer");
                     break;
                default:
                     break;
            }
        }
        mv_sum = 0;
        for (int j=0; j < SAMPLES_PER_MEASURE; j++){
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_VBAT_CHANNEL, &mv));
            mv_sum += mv;
        }
        adc_msg.adc_raw = mv_sum / SAMPLES_PER_MEASURE;

        if (do_calibration1) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, adc_msg.adc_raw, &mv));
            adc_msg.voltage = ((float)mv/1000.0)*ADC_CHANNEL_CONV_COEF.mult;
            adc_msg.capacity_percent = volt_to_capacity_percent(adc_msg.voltage);
            adc_msg.cycles = charge_discharge_cycles;
            adc_msg.bat_mode = bat_running_mode;
        }
        if(++sec_counter >= 5){
            sec_counter=0;
            float cur_total_voltage = adc_msg.voltage;
            if ((cur_total_voltage - prev_total_volt) > 0.05 || 
                cur_total_voltage >= MAX_CELL_VOLT - 0.01){
                if (bat_running_mode != BAT_CHARGE_MODE){
                    bat_running_mode = BAT_CHARGE_MODE;
                    if(adc_sleep){
                        ESP_LOGI(TAG, "Wakeup subtasks");
                        adc_sleep = adc_wakeup_subtasks();
                    }
                }
                prev_total_volt = cur_total_voltage;
            }

            if ((cur_total_voltage - prev_total_volt) < -0.05) {
                if (bat_running_mode != BAT_DISCHARGE_MODE){
                    ESP_LOGI(TAG,"Entering sleep mode");
                    bat_running_mode = BAT_DISCHARGE_MODE;
                    adc_sleep = adc_sleep_subtasks();
                    vTaskDelay(pdMS_TO_TICKS(5000));
                }
                prev_total_volt = cur_total_voltage;
            }
            float volt_diff = cur_total_voltage - prev_total_volt;
            if(volt_diff < 0.0) volt_diff *= -1.0; 
            if(volt_diff <= 0.05){
                if (bat_running_mode != BAT_HOLD_MODE){
                    bat_running_mode = BAT_HOLD_MODE;
                }
            }

            ESP_LOGI(TAG, "Total voltage: %.2f Battery mode: %s", 
                     cur_total_voltage,
                     bat_running_mode ? "DISCHARGE" : "CHARGE");
        }

        if(!adc_sleep) 
            vTaskDelay(pdMS_TO_TICKS(1000));
        else {
            rtc_gpio_pullup_en(GPIO_NUM_15);
            ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(GPIO_NUM_15,0));
            ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(1000000));
            esp_deep_sleep_start();
        }
    }
}
