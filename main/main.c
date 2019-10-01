/*
IOTA cashier who monitoring the balance of an address.
*/

#include <inttypes.h>
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

#include "cashier.h"

// The BOOT butten on board, push on LOW.
#define WAKE_UP_GPIO GPIO_NUM_0
// ESP32-DevKitC V4 onboard LED
#define BLINK_GPIO GPIO_NUM_2

static char const *TAG = "main";

static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const static int CONNECTED_BIT = BIT0;

// log previous balance
uint64_t latest_balance = 0;

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

static void initialize_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

static void restart_in(int second) {
  for (int i = second; i >= 0; i--) {
    ESP_LOGI(TAG, "Restarting in %d seconds...", i);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  ESP_LOGI(TAG, "Restarting now.\n");
  fflush(stdout);
  esp_restart();
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

  if (timeinfo.tm_year < (2018 - 1900)) {
    ESP_LOGE(TAG, "Sync SNPT failed...");
    lcd_print(1, 10, COLOR_RED, "Get time failed.");
    restart_in(5);
  }

  // set timezone
  char strftime_buf[32];
  setenv("TZ", CONFIG_SNTP_TZ, 1);
  tzset();
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
}

// validats the address or seed length in the configure file.
void check_receiver_address() {
  size_t hash_len = 0;
#ifdef CONFIG_ADDRESS_REFRESH
  hash_len = strlen(CONFIG_IOTA_SEED);
#else
  hash_len = strlen(CONFIG_IOTA_RECEIVER);
#endif
  if (!(hash_len == HASH_LENGTH_TRYTE || hash_len == HASH_LENGTH_TRYTE + 9)) {
    lcd_print(1, 4, COLOR_RED, "Invalid hash");
    lcd_print(1, 6, COLOR_RED, "Restart in 5s");
    ESP_LOGE(TAG, "please set a valid hash(CONFIG_IOTA_RECEIVER or CONFIG_IOTA_SEED) in sdkconfig!");
    restart_in(5);
  }
}

void monitor_receiver_address() {
  uint64_t curr_balance = get_balance();
  if (curr_balance > latest_balance) {
    // the balance has increased.
    printf("\033[0;34m+ %" PRIu64 "i\033[0m\n", curr_balance - latest_balance);
    show_balace(curr_balance);
  } else if (curr_balance == latest_balance) {
    // the balance has remained the same.
    printf("= %" PRIu64 "i\n", latest_balance);
  } else {
    printf("\033[1;31m- %" PRIu64 "i\033[0m\n", latest_balance - curr_balance);
    // this address has become a spent address, seeking for a new unspent one.
    lcd_fill_screen(COLOR_WHITE);
    lcd_print(1, 4, COLOR_RED, "Update Address...");
    update_receiver_address();
    curr_balance = get_balance();
    lcd_draw_qrcode();
    show_balace(curr_balance);
  }
  latest_balance = curr_balance;
}

void app_main() {
  // init GPIO
  gpio_pad_select_gpio(BLINK_GPIO);
  gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(BLINK_GPIO, 1);

  // init lcd
  lcd_init();
  lcd_fill_screen(COLOR_WHITE);

  lcd_print(1, 2, COLOR_BLACK, "Checking hash...");
  check_receiver_address();

  initialize_nvs();

  lcd_print(1, 4, COLOR_BLACK, "Init WiFi...");
  // init wifi
  wifi_conn_init();

  /* Wait for the callback to set the CONNECTED_BIT in the event group. */
  xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
  ESP_LOGI(TAG, "WiFi Connected");
  ESP_LOGI(TAG, "IRI Node: %s, port: %d, HTTPS:%s\n", CONFIG_IRI_NODE_URI, CONFIG_IRI_NODE_PORT,
           CONFIG_ENABLE_HTTPS ? "True" : "False");

  lcd_print(1, 6, COLOR_BLACK, "SSID: %s", CONFIG_WIFI_SSID);
  // get time from sntp
  update_time();

  // init cclient
  lcd_print(1, 8, COLOR_BLACK, "Init Address...");
  if (init_iota_client() != RC_OK) {
    lcd_print(1, 10, COLOR_RED, "Initial client failed");
    lcd_print(1, 12, COLOR_RED, "Restart in 5s");
    ESP_LOGE(TAG, "initial client or unable to find an unused address.");
    restart_in(5);
  }

  latest_balance = get_balance();
  ESP_LOGI(TAG, "Initial balance: %" PRIu64 "i, interval %d", latest_balance, CONFIG_INTERVAL);

  lcd_print(1, 10, COLOR_BLUE, "Ready to go...");
  vTaskDelay(2 * 1000 / portTICK_PERIOD_MS);
  lcd_fill_screen(COLOR_WHITE);

  lcd_draw_qrcode();
  show_balace(latest_balance);

  while (1) {
    vTaskDelay(CONFIG_INTERVAL * 1000 / portTICK_PERIOD_MS);
    monitor_receiver_address();
  }
}
