#include <stdio.h>
#include <stdlib.h>
#include <string.h> //Requires by memset
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_system.h"
#include "spi_flash_mmap.h"
#include <esp_http_server.h>
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "connect_wifi.h"
#include "adc_poll.h"

#define LED_PIN 5

static const char *TAG = "espressif"; // TAG for debug
int led_state = 0;

#define INDEX_HTML_PATH "/spiffs/index.html"
char index_html[8192];
char update_json[256];
extern ADC_MESSAGE adc_msg[POLL_CHANNELS_NUM];
//char response_data[8192];

static void initi_web_page_buffer(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = false};

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    memset((void *)index_html, 0, sizeof(index_html));
    memset((void *)update_json, 0, sizeof(update_json));
    struct stat st;
    if (stat(INDEX_HTML_PATH, &st))
    {
        ESP_LOGE(TAG, "index.html not found");
        return;
    }

    FILE *fp = fopen(INDEX_HTML_PATH, "r");
    if (fread(index_html, st.st_size, 1, fp) == 0)
    {
        ESP_LOGE(TAG, "fread failed");
    }
    fclose(fp);
}

esp_err_t send_web_page(httpd_req_t *req)
{
    int response;
    //sprintf(response_data, index_html);
    response = httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return response;
}

esp_err_t update_web_page(httpd_req_t *req){
    int response;
    sprintf(update_json, "{\"voltage-1\": %.2f, \"voltage-2\": %.2f, \"voltage-3\": %.2f, \"voltage-4\": %.2f, \"current\":%.2f, \"power\":%.2f, \"capacity\":2200}",
    adc_msg[0].voltage, adc_msg[1].voltage, adc_msg[2].voltage, adc_msg[3].voltage, adc_msg[4].voltage,
    (adc_msg[0].voltage + adc_msg[1].voltage + adc_msg[2].voltage) * adc_msg[3].voltage);
    response = httpd_resp_send(req, update_json, HTTPD_RESP_USE_STRLEN);
    return response;
}

esp_err_t get_req_handler(httpd_req_t *req)
{
    return send_web_page(req);
}

esp_err_t update_handler(httpd_req_t *req)
{
    return update_web_page(req);
}

httpd_uri_t uri_get = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = get_req_handler,
    .user_ctx = NULL};

httpd_uri_t uri_update = {
    .uri = "/update",
    .method = HTTP_GET,
    .handler = update_handler,
    .user_ctx = NULL};

httpd_handle_t setup_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_update);
    }
    return server;
}

void app_main()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    connect_wifi();
    // GPIO initialization

    if (wifi_connect_status)
    {
        esp_rom_gpio_pad_select_gpio(LED_PIN);
        gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

        led_state = 0;
        ESP_LOGI(TAG, "LED Control SPIFFS Web Server is running ... ...\n");
        initi_web_page_buffer();
        setup_server();
    }
    xTaskCreate(&adc_poll_task, "adc_task", 4096, NULL, 5, NULL);
}