#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "adc_poll.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define ADC_POLL_ATTEN           ADC_ATTEN_DB_12

const static char *TAG = "ADC_POLLING";
const static adc_channel_t ADC_CHANNELS[POLL_CHANNELS_NUM] = {
    ADC_CHANNEL_0,
    ADC_CHANNEL_3,
    ADC_CHANNEL_6,
    ADC_CHANNEL_7
};

const static ADC_COEF ADC_CHANNEL_CONV_COEF[POLL_CHANNELS_NUM] = {
    {0.0, 0.002},
    {0.0, 0.002},
    {0.0, 0.002},
    {}
};

ADC_MESSAGE adc_msg[POLL_CHANNELS_NUM];

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

void adc_poll_task(void *pvParameter) {
    unsigned int i;
    int mv;
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
    for (i=0; i<POLL_CHANNELS_NUM; i++){
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNELS[i], &config));
    }

    //-------------ADC1 Calibration Init---------------//
    adc_cali_handle_t adc1_cali_handle[POLL_CHANNELS_NUM] = {
        NULL,
        NULL,
        NULL,
        NULL
    };
    bool do_calibration1[POLL_CHANNELS_NUM] = {
        false,
        false,
        false,
        false
    };

    for (i=0; i < POLL_CHANNELS_NUM; i++){
        do_calibration1[i] = adc_poll_calibration_init(ADC_UNIT_1, ADC_CHANNELS[i], ADC_POLL_ATTEN, &adc1_cali_handle[i]);
    }
    i = 0;
    while(1){
        for (i=0; i < POLL_CHANNELS_NUM; i++){
            ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, ADC_CHANNELS[i], &adc_msg[i].adc_raw));
            ESP_LOGI(TAG, "ADC%d Channel[%d] Raw Data: %d", ADC_UNIT_1 + 1, ADC_CHANNELS[i], adc_msg[i].adc_raw);
            if (do_calibration1[i]) {
                ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle[i], adc_msg[i].adc_raw, &mv));
                adc_msg[i].voltage=(float)mv/1000.0;
                ESP_LOGI(TAG, "ADC%d Channel[%d] Cali Voltage: %.2f V", ADC_UNIT_1 + 1, ADC_CHANNELS[i], adc_msg[i].voltage);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
