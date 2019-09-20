/*
IOTA cashier who monitoring the balance of an address.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "rom/uart.h"

// sntp
#include "lwip/apps/sntp.h"
#include "lwip/err.h"

// wifi
#include "esp_event_loop.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"

#include "cclient/api/core/core_api.h"

// The BOOT butten on board, push on LOW.
#define WAKE_UP_GPIO GPIO_NUM_0
// ESP32-DevKitC V4 onboard LED
#define BLINK_GPIO GPIO_NUM_2

static const char *TAG = "cashier";

static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const static int CONNECTED_BIT = BIT0;

// log previous balance
static uint64_t latest_balance = 0;

//================CClient Setup=============
static iota_client_service_t g_cclient;

static char const *amazon_ca1_pem =
    "-----BEGIN CERTIFICATE-----\r\n"
    "MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF\r\n"
    "ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6\r\n"
    "b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL\r\n"
    "MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv\r\n"
    "b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj\r\n"
    "ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM\r\n"
    "9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw\r\n"
    "IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6\r\n"
    "VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L\r\n"
    "93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm\r\n"
    "jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC\r\n"
    "AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA\r\n"
    "A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI\r\n"
    "U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs\r\n"
    "N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv\r\n"
    "o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU\r\n"
    "5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy\r\n"
    "rqXRfboQnoZsG4q5WTP468SQvvG5\r\n"
    "-----END CERTIFICATE-----\r\n";

static void init_iota_client() {
  g_cclient.http.path = "/";
  g_cclient.http.content_type = "application/json";
  g_cclient.http.accept = "application/json";
  g_cclient.http.host = CONFIG_IRI_NODE_URI;
  g_cclient.http.port = CONFIG_IRI_NODE_PORT;
  g_cclient.http.api_version = 1;
#ifdef CONFIG_ENABLE_HTTPS
  g_cclient.http.ca_pem = amazon_ca1_pem;
#else
  g_cclient.http.ca_pem = NULL;
#endif
  g_cclient.serializer_type = SR_JSON;
  iota_client_core_init(&g_cclient);
}

static uint64_t get_balance() {
  retcode_t ret_code = RC_OK;
  flex_trit_t tmp_hash[FLEX_TRIT_SIZE_243];
  get_balances_req_t *balance_req = get_balances_req_new();
  get_balances_res_t *balance_res = get_balances_res_new();
  uint64_t balance = 0;

  if (!balance_req || !balance_res) {
    printf("Error: OOM\n");
    goto done;
  }

  if (flex_trits_from_trytes(tmp_hash, NUM_TRITS_HASH, (tryte_t const *)CONFIG_IOTA_RECEIVER, NUM_TRYTES_HASH,
                             NUM_TRYTES_HASH) == 0) {
    printf("Error: converting flex_trit failed\n");
    goto done;
  }

  if ((ret_code = hash243_queue_push(&balance_req->addresses, tmp_hash)) != RC_OK) {
    printf("Error: Adding hash to list failed!\n");
    goto done;
  }

  balance_req->threshold = 100;

  if ((ret_code = iota_client_get_balances(&g_cclient, balance_req, balance_res)) == RC_OK) {
    balance = get_balances_res_balances_at(balance_res, 0);
  }

done:
  if(ret_code != RC_OK){
    printf("get_balance: %s\n", error_2_string(ret_code));
  }
  get_balances_req_free(&balance_req);
  get_balances_res_free(&balance_res);
  return balance;
}

//===========End of CClient=================

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event) {
  switch (event->event_id) {
    case SYSTEM_EVENT_STA_START:
      esp_wifi_connect();
      break;
    case SYSTEM_EVENT_STA_GOT_IP:
      xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
      break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
      /* This is a workaround as ESP32 WiFi libs don't currently
             auto-reassociate. */
      esp_wifi_connect();
      xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
      break;
    default:
      break;
  }
  return ESP_OK;
}

static void wifi_conn_init(void) {
  tcpip_adapter_init();
  wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = CONFIG_WIFI_SSID,
              .password = CONFIG_WIFI_PASSWORD,
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
}

static void check_receiver_address() {
  if (strlen(CONFIG_IOTA_RECEIVER) != HASH_LENGTH_TRYTE) {
    ESP_LOGE(TAG, "please set a valid address hash(CONFIG_IOTA_RECEIVER) in sdkconfig!");
    for (int i = 30; i >= 0; i--) {
      ESP_LOGI(TAG, "Restarting in %d seconds...", i);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "Restarting now.\n");
    fflush(stdout);
    esp_restart();
  }
}

static void monitor_receiver_address() {
  uint64_t curr_balance = get_balance();
  if (curr_balance > latest_balance) {
    printf("\033[0;34m+ %" PRIu64 "i\033[0m\n", curr_balance - latest_balance);
    // TODO
  } else if (curr_balance == latest_balance) {
    printf("= %" PRIu64 "i\n", latest_balance);
    // TODO
  } else {
    printf("\033[1;31m- %" PRIu64 "i\033[0m\n", latest_balance - curr_balance);
    // TODO
  }
  latest_balance = curr_balance;
}

static void initialize_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

static void update_time() {
  // init sntp
  ESP_LOGI(TAG, "Initializing SNTP: %s, Timezone: %s", CONFIG_SNTP_SERVER, CONFIG_SNTP_TZ);
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, CONFIG_SNTP_SERVER);
  sntp_init();

  // wait for time to be set
  time_t now = 0;
  struct tm timeinfo = {0};
  int retry = 0;
  const int retry_count = 10;
  while (timeinfo.tm_year < (2018 - 1900) && ++retry < retry_count) {
    ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    time(&now);
    localtime_r(&now, &timeinfo);
  }

  // set timezone
  char strftime_buf[32];
  setenv("TZ", CONFIG_SNTP_TZ, 1);
  tzset();
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
}

void app_main() {
  // init GPIO
  gpio_pad_select_gpio(BLINK_GPIO);
  gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(BLINK_GPIO, 1);

  check_receiver_address();

  initialize_nvs();
  // init wifi
  wifi_conn_init();

  ESP_LOGI(TAG, "Connecting to %s...", CONFIG_WIFI_SSID);
  /* Wait for the callback to set the CONNECTED_BIT in the event group. */
  xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
  ESP_LOGI(TAG, "WiFi Connected");
  ESP_LOGI(TAG, "IRI Node: %s, port: %d, HTTPS:%s\n", CONFIG_IRI_NODE_URI, CONFIG_IRI_NODE_PORT,
           CONFIG_ENABLE_HTTPS ? "True" : "False");

  // get time from sntp
  update_time();

  // init cclient
  init_iota_client();
  latest_balance = get_balance();
  ESP_LOGI(TAG, "Receive: %s", CONFIG_IOTA_RECEIVER);
  ESP_LOGI(TAG, "Initial balance: %" PRIu64 "i, interval %d", latest_balance, CONFIG_INTERVAL);

  while (1) {
    vTaskDelay(CONFIG_INTERVAL * 1000 / portTICK_PERIOD_MS);
    monitor_receiver_address();
  }
}
