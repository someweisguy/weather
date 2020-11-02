#include "wireless.h"

#include "cJSON.h"
#include "esp_http_client.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "smartconfig.h"
#include <string.h>

typedef struct
{
    char *mqtt_broker;
} connect_args_t;

static esp_mqtt_client_handle_t mqtt_client = NULL;
static TaskHandle_t calling_task = NULL;
static bool mqtt_is_connected = false;

static void sntp_callback(struct timeval *tv)
{
    xTaskNotify(calling_task, 0, eNoAction);
}

static esp_err_t mqtt_handler(esp_mqtt_event_handle_t event)
{
    if (event->event_id == MQTT_EVENT_CONNECTED)
    {
        mqtt_is_connected = true;
        //mqtt_publish("online", "Hello world!", 2, false);
    }
    else if (event->event_id == MQTT_EVENT_DISCONNECTED)
    {
        mqtt_is_connected = false;
    }
    return ESP_OK;
}

static void wifi_handler(void *handler_args, esp_event_base_t base, int event_id, void *event_data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        const wifi_event_sta_disconnected_t *wifi_data = event_data;

        // if bad password start smart config
        if (wifi_data->reason == WIFI_REASON_AUTH_FAIL)
            smartconfig_start();
        else
            esp_wifi_connect();

        // stop the mqtt client if it is running
        if (mqtt_client != NULL)
            esp_mqtt_client_stop(mqtt_client);
    }
    else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        if (mqtt_client == NULL)
        {
            // this is the first time we're connecting
            // get the connect args
            connect_args_t *connect_args = (connect_args_t *)handler_args;

            // start the mqtt client
            const esp_mqtt_client_config_t config = {
                .uri = connect_args->mqtt_broker,
                .event_handle = mqtt_handler};
            mqtt_client = esp_mqtt_client_init(&config);

            // connect to the sntp server and synchronize the time
            sntp_set_time_sync_notification_cb(sntp_callback);
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setservername(0, "time.google.com");
            sntp_init();
        }

        esp_mqtt_client_start(mqtt_client);
    }
}

esp_err_t wireless_start(const char *mqtt_broker)
{
    // init network interface and wifi sta
    esp_netif_init();
    esp_netif_create_default_wifi_sta();

    // init wifi driver
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init_config);

    // allocate connect args
    connect_args_t *connect_args = malloc(sizeof(connect_args_t));
    connect_args->mqtt_broker = strdup(mqtt_broker);

    // register wifi event handlers
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, (void *)connect_args);

    // get wifi credentials from nvs
    wifi_config_t wifi_config = {};
    esp_err_t err = esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);
    if (err)
        return err;

    // check that an ssid was found
    if (strlen((const char *)(wifi_config.sta.ssid)) > 0)
    {
        // configure and start wifi
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
        esp_wifi_start();
    }
    else
        // start smartconfig
        smartconfig_start();

    // wait for sntp to sync
    calling_task = xTaskGetCurrentTaskHandle();
    xTaskNotifyWait(0, 0, 0, portMAX_DELAY);

    return ESP_OK;
}

esp_err_t mqtt_publish(const char *topic, const char *message, int qos, bool retain)
{
    if (mqtt_client == NULL)
        return ESP_ERR_INVALID_STATE;
    const int ret = esp_mqtt_client_publish(mqtt_client, topic, message,
                                            0, qos, retain);
    return ret == -1 ? ESP_FAIL : ESP_OK;
}

esp_err_t mqtt_publish_json(const char *topic, cJSON *json, int qos, bool retain)
{
    char *message = cJSON_PrintUnformatted(json);
    const esp_err_t err = mqtt_publish(topic, message, qos, retain);
    free(message);
    return err;
}

int8_t wireless_get_rssi()
{
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK)
        return 0;
    return ap_info.rssi;
}

double wireless_get_elevation()
{
    // configure the first http request and client
    esp_http_client_config_t config = {
        .url = "http://ipinfo.io/json"};
    esp_http_client_handle_t client = esp_http_client_init(&config);

    double elevation = 0;
    do
    {
        // send the first http request
        float latitude, longitude;
        esp_err_t err = esp_http_client_open(client, 0);
        if (!err)
        {
            esp_http_client_fetch_headers(client);

            // read the response into a buffer
            // response is not chunked, so check headers for content-length
            const int content_length = esp_http_client_get_content_length(client);
            cJSON *json = NULL;
            if (content_length < 1)
                break;

            char *content = malloc(content_length + 1);
            esp_http_client_read(client, content, content_length);
            content[content_length] = 0;
            json = cJSON_Parse(content);
            free(content);

            // parse latitude and longitude
            cJSON *loc = cJSON_GetObjectItem(json, "loc");
            if (loc == NULL)
                break;
            sscanf(loc->valuestring, "%f,%f", &latitude, &longitude);
            cJSON_Delete(json);
        }

        // configure the second http request
        const char *url_format = "https://nationalmap.gov/epqs/pqs.php?x=%.6f&y=%.6f&units=Meters&output=json";
        char url[strlen(url_format) + (11 * 2)];
        sprintf(url, url_format, longitude, latitude);
        err = esp_http_client_set_url(client, url);

        // send the second http request
        err = esp_http_client_open(client, 0);
        if (!err)
        {
            // read the response into a buffer
            // response is chunked, and esp-idf v4.1 doesn't handle chunks so well,
            //  so we allocate a buffer around the right size for the response.
            const int status_code = esp_http_client_get_status_code(client);
            cJSON *json = NULL;
            if (status_code != 200)
                break;
            const size_t BUFFER_SIZE = 255;
            char *content = malloc(BUFFER_SIZE);
            const int read = esp_http_client_read(client, content, BUFFER_SIZE);
            content[read] = 0;
            json = cJSON_Parse(content);
            free(content);

            // parse elevation in meters
            cJSON *node = cJSON_GetObjectItem(json, "USGS_Elevation_Point_Query_Service");
            node = cJSON_GetObjectItem(node, "Elevation_Query");
            node = cJSON_GetObjectItem(node, "Elevation");
            if (node == NULL)
                break;
            elevation = node->valuedouble;
            cJSON_Delete(json);
        }
    } while (false);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return elevation;
}

esp_err_t mqtt_publish_discovery(const char *topic, mqtt_discovery_t discovery)
{
    cJSON *json = cJSON_CreateObject();

    if (discovery.state_topic == NULL)
        return ESP_ERR_INVALID_ARG;

    // TODO

    esp_err_t err = mqtt_publish_json(topic, json, 2, true);
    cJSON_Delete(json);
    return err;
}