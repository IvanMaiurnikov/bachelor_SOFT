#include "spi_flash_mmap.h"
#include <esp_http_server.h>
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "wifi_task.h"
#include "adc_task.h"

#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define LED_PIN 5

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define INDEX_HTML_PATH "/spiffs/index.html"

static int s_retry_num = 0;
static int led_state = 0;
int wifi_connect_status = 0;
int sleep_state = 0;
static const char *TAG = "WIFI_TASK"; // TAG for debug

char index_html[8192];
char update_json[256];
extern ADC_MESSAGE adc_msg[POLL_CHANNELS_NUM];
TaskHandle_t TaskHandlerWifi = NULL;

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

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_retry_num < CONFIG_ESP_MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "retry to connect to the AP");
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
            wifi_connect_status = 0;
            ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_connect_status = 1;
    }
}

void connect_wifi(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT){
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT){
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
    vEventGroupDelete(s_wifi_event_group);
}

void wifi_task(void *pvParameter){
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    uint32_t notif_val;
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    while(1){
        connect_wifi();
        // GPIO initialization
        if (wifi_connect_status) {
            esp_rom_gpio_pad_select_gpio(LED_PIN);
            gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
            led_state = 0;
            ESP_LOGI(TAG, "Web Server is running ... ...\n");
            initi_web_page_buffer();
            //server = setup_server();
            if (ESP_OK == httpd_start(&server, &config)){
                httpd_register_uri_handler(server, &uri_get);
                httpd_register_uri_handler(server, &uri_update);
            }
        }
        if( pdTRUE == xTaskNotifyWait(0x00, 0x00, &notif_val, (TickType_t)portMAX_DELAY)){
            if (notif_val == NOTIFY_SLEEP_WIFI){
                ESP_LOGI(TAG, "Entering WiFi sleep mode\n");
                if (sleep_state == 0){
                    sleep_state = 1;
                    if (server){
                        httpd_unregister_uri(server, uri_get.uri);
                        httpd_unregister_uri(server, uri_update.uri);
                        if(ESP_OK == httpd_stop(server)){
                            ESP_LOGI(TAG, "HTTP server stopped");
                        }
                    }
                    ESP_LOGI(TAG, "Stopping WiFi");
                    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler);
                    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler);
                    esp_wifi_disconnect();
                    esp_err_t rc = esp_wifi_stop();
                }
            }
        }

        if( pdTRUE == xTaskNotifyWait(0x00, 0x00, &notif_val, (TickType_t)portMAX_DELAY)){
            if (notif_val == NOTIFY_WAKE_WIFI){
                sleep_state = 0;
                ESP_LOGI(TAG, "Waking up WiFi\n");
            }
        }
    }
}