#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "spi_flash_mmap.h"
#include <esp_http_server.h>
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "wifi_task.h"
#include "adc_task.h"

#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define INDEX_HTML_PATH "/spiffs/index.html"

static int s_retry_num = 0;
static int wifi_connect_status = 0,
           wifi_sleep_state = 0;
static esp_netif_t *sta_netif = NULL;
static SemaphoreHandle_t s_semph_get_ip_addrs = NULL;

static const char *TAG = "WIFI_TASK"; // TAG for debug

char index_html[8192];
char update_json[256];
extern ADC_MESSAGE adc_msg;
TaskHandle_t wifi_handler = NULL;

static void initi_web_page_buffer(void) {
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
    sprintf(update_json, "{\"voltage\": %.2f, \"mode\": %d, \"cycles\": %d, \"percent\": %d}", adc_msg.voltage, adc_msg.bat_mode, adc_msg.cycles, adc_msg.capacity_percent);
    ESP_LOGI(TAG, "%s", update_json);
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

static void event_handler_on_wifi_disconnect(void *arg, esp_event_base_t event_base,
                                             int32_t event_id, void *event_data){
    s_retry_num++;
    if (s_retry_num > CONFIG_WIFI_CONN_MAX_RETRY) {
        ESP_LOGI(TAG, "WiFi Connect failed %d times, stop reconnect.", s_retry_num);
        /* let wifi_sta_do_connect() return */
        if (s_semph_get_ip_addrs) {
            xSemaphoreGive(s_semph_get_ip_addrs);
        }
        wifi_connect_status = 0;
        return;
    }
    ESP_LOGI(TAG, "Wi-Fi disconnected, trying to reconnect...");
    esp_err_t err = esp_wifi_connect();
    if (err == ESP_ERR_WIFI_NOT_STARTED) {
        wifi_connect_status = 0;
        return;
    }
    ESP_ERROR_CHECK(err);
}

static void event_handler_on_sta_got_ip(void *arg, esp_event_base_t event_base,
                                             int32_t event_id, void *event_data){
    s_retry_num = 0;
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

    ESP_LOGI(TAG, "Got IPv4 event: Interface \"%s\" address: " IPSTR, esp_netif_get_desc(event->esp_netif), IP2STR(&event->ip_info.ip));
    if (s_semph_get_ip_addrs) {
        xSemaphoreGive(s_semph_get_ip_addrs);
    } else {
        ESP_LOGI(TAG, "- IPv4 address: " IPSTR ",", IP2STR(&event->ip_info.ip));
    }
    wifi_connect_status = 1;
}

static void event_handler_on_wifi_connect(void *arg, esp_event_base_t event_base,
                                             int32_t event_id, void *event_data){
}

esp_err_t wifi_sta_do_connect(wifi_config_t wifi_config, bool wait) {
    if (wait) {
        s_semph_get_ip_addrs = xSemaphoreCreateBinary();
        if (s_semph_get_ip_addrs == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    s_retry_num = 0;

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler_on_wifi_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler_on_sta_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &event_handler_on_wifi_connect, sta_netif));

    ESP_LOGI(TAG, "Connecting to %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect failed! ret:%x", ret);
        return ret;
    }
    if (wait) {
        ESP_LOGI(TAG, "Waiting for IP(s)");
#if CONFIG_EXAMPLE_CONNECT_IPV4
        xSemaphoreTake(s_semph_get_ip_addrs, portMAX_DELAY);
#endif

        if (s_retry_num > CONFIG_WIFI_CONN_MAX_RETRY) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}
static esp_err_t  connect_wifi(void) {
    ESP_LOGI(TAG, "Connect WiFi started...");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
    // Warning: the interface desc is used in tests to capture actual connection details (IP, gw, mask)
    esp_netif_config.if_desc = "netif_sta";
    esp_netif_config.route_prio = 128;
    sta_netif = esp_netif_create_wifi(WIFI_IF_STA, &esp_netif_config);
    esp_wifi_set_default_wifi_sta_handlers();

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
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
    return (ESP_OK == wifi_sta_do_connect(wifi_config, true)) ? 1 : 0;
}

esp_err_t wifi_sta_do_disconnect(void) {
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler_on_wifi_disconnect));
    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler_on_sta_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &event_handler_on_wifi_connect));
    if (s_semph_get_ip_addrs) {
        vSemaphoreDelete(s_semph_get_ip_addrs);
    }
    return esp_wifi_disconnect();
}

static void wifi_stop(void) {
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        return;
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_wifi_deinit());
    ESP_ERROR_CHECK(esp_wifi_clear_default_wifi_driver_and_handlers(sta_netif));
    esp_netif_destroy(sta_netif);
    sta_netif = NULL;
}

static void wifi_shutdown(void) {
    wifi_sta_do_disconnect();
    wifi_stop();
}

void wifi_task(void *pvParameter){
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    uint32_t notif_val;
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);


    wifi_connect_status = connect_wifi();
    if (wifi_connect_status) {
        ESP_LOGI(TAG, "Web Server is running ... ...\n");
        initi_web_page_buffer();
        //server = setup_server();
        if (ESP_OK == httpd_start(&server, &config)){
            httpd_register_uri_handler(server, &uri_get);
            httpd_register_uri_handler(server, &uri_update);
        }
    }

    while( pdTRUE == xTaskNotifyWait(0x00, 0x00, &notif_val, (TickType_t)portMAX_DELAY)){
        if (notif_val == NOTIFY_SLEEP_WIFI){
            ESP_LOGI(TAG, "Entering WiFi sleep mode\n");
            if (server){
                httpd_unregister_uri(server, uri_get.uri);
                httpd_unregister_uri(server, uri_update.uri);
                if(ESP_OK == httpd_stop(server)){
                    ESP_LOGI(TAG, "HTTP server stopped");
                }
            }
            wifi_shutdown();
            ESP_LOGI(TAG, "WiFi stopped");
            break;
        }
    }
    vTaskDelete(NULL);
}