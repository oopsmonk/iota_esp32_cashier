#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cashier.h"
#include "common/helpers/checksum.h"
#include "qrcodegen.h"
#include "sdkconfig.h"

static char const *TAG = "cashier";

//================CClient Setup=============
static iota_client_service_t g_cclient;
// address with checksum
static flex_trit_t unspent_addr[NUM_FLEX_TRITS_ADDRESS];
static uint64_t unspent_index = 0;

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

retcode_t update_receiver_address() {
#ifdef CONFIG_ADDRESS_REFRESH
  retcode_t ret = RC_ERROR;
  flex_trit_t seed[FLEX_TRIT_SIZE_243];
  address_opt_t opt = {.security = CONFIG_IOTA_SECURITY, .start = CONFIG_IOTA_ADDRESS_START_INDEX, .total = UINT64_MAX};

  if (unspent_index != 0) {
    opt.start = unspent_index;
  }
  ESP_LOGI(TAG, "Get unspent address from %" PRIu64 "\n", opt.start);

  if (flex_trits_from_trytes(seed, NUM_TRITS_HASH, (tryte_t const *)CONFIG_IOTA_SEED, NUM_TRYTES_HASH,
                             NUM_TRYTES_HASH) != 0) {
    // seeking for an unspent address
    if ((ret = iota_client_get_unspent_address(&g_cclient, seed, opt, unspent_addr, &unspent_index) != RC_OK)) {
      ESP_LOGE(TAG, "Get unspent address failed.\n");
    }
  } else {
    ESP_LOGE(TAG, "flex_trits converting failed.\n");
    ret = RC_CCLIENT_FLEX_TRITS;
  }
  return ret;
#else
  if (flex_trits_from_trytes(unspent_addr, NUM_TRITS_ADDRESS, (tryte_t const *)CONFIG_IOTA_RECEIVER, NUM_TRYTES_ADDRESS,
                             NUM_TRYTES_ADDRESS) == 0) {
    ESP_LOGE(TAG, "flex_trits converting failed.\n");
    return RC_CCLIENT_FLEX_TRITS;
  }
  return RC_OK;
#endif
}

// initialize CClient lib and get an unused address.
retcode_t init_iota_client() {
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

  return update_receiver_address();
}

uint64_t get_balance() {
  retcode_t ret_code = RC_OK;
  get_balances_req_t *balance_req = get_balances_req_new();
  get_balances_res_t *balance_res = get_balances_res_new();
  uint64_t balance = 0;

  if (!balance_req || !balance_res) {
    ESP_LOGE(TAG, "Error: OOM\n");
    goto done;
  }

  if ((ret_code = hash243_queue_push(&balance_req->addresses, unspent_addr)) != RC_OK) {
    ESP_LOGE(TAG, "Error: Adding hash to list failed!\n");
    goto done;
  }

  balance_req->threshold = 100;
#ifdef CONFIG_ADDRESS_REFRESH
  printf("Get balance [%" PRIu64 "]", unspent_index);
  flex_trit_print(unspent_addr, NUM_TRITS_ADDRESS);
  printf("\n");
#endif

  if ((ret_code = iota_client_get_balances(&g_cclient, balance_req, balance_res)) == RC_OK) {
    balance = get_balances_res_balances_at(balance_res, 0);
  }

done:
  if (ret_code != RC_OK) {
    ESP_LOGE(TAG, "get_balance: %s\n", error_2_string(ret_code));
  }
  get_balances_req_free(&balance_req);
  get_balances_res_free(&balance_res);
  return balance;
}

//===========End of CClient=================

//===========LCD and QR code================

#ifdef CONFIG_FTF_LCD
// text buffer for display
static char lcd_text[32] = {};
#endif

void lcd_init() {
#ifdef CONFIG_FTF_LCD
  st7735_init();
#endif
}

void lcd_fill_screen(int16_t color) {
#ifdef CONFIG_FTF_LCD
  st7735_fill_screen(COLOR_WHITE);
#endif
}

void lcd_print(int16_t x, int16_t y, int16_t color, const char *fmt, ...) {
#ifdef CONFIG_FTF_LCD
  va_list arg_list;
  va_start(arg_list, fmt);
  vsprintf(lcd_text, fmt, arg_list);
  va_end(arg_list);
  st7735_draw_string(x, y, lcd_text, color, COLOR_WHITE, 1);
#endif
}

void show_balace(uint64_t balance) {
#ifdef CONFIG_FTF_LCD
  // show balance on LCD
  if (balance > 1000000000000) {
    sprintf(lcd_text, "%-3.2fTi", (float)balance / 1000000000000);
  } else if (balance > 1000000000) {
    sprintf(lcd_text, "%-3.2fGi", (float)balance / 1000000000);
  } else if (balance > 1000000) {
    sprintf(lcd_text, "%-3.2fMi", (float)balance / 1000000);
  } else if (balance > 1000) {
    sprintf(lcd_text, "%-3.2fKi", (float)balance / 1000);
  } else {
    sprintf(lcd_text, "%-3di", (int)balance);
  }
  st7735_draw_string(2, 1, lcd_text, COLOR_BLUE, COLOR_WHITE, 2);
#endif
}

void lcd_draw_qrcode() {
#ifdef CONFIG_FTF_LCD
  int element_size = 2;
  int qr_version = 10;
  // qr code (x,y) offset
  int offset_x = 8, offset_y = 30;
  size_t qr_buff_len = qrcodegen_BUFFER_LEN_FOR_VERSION(qr_version);
  uint8_t qr0[qr_buff_len];
  uint8_t tempBuffer[qr_buff_len];

#ifdef CONFIG_ADDRESS_REFRESH
  size_t sum_len = 9;
  char *checksum = NULL;
  tryte_t address_string[NUM_TRYTES_ADDRESS + sum_len + 1];
  flex_trits_to_trytes(address_string, NUM_TRYTES_ADDRESS, unspent_addr, NUM_TRITS_ADDRESS, NUM_TRITS_ADDRESS);

  checksum = iota_checksum((char *)address_string, NUM_TRYTES_ADDRESS, sum_len);
  memcpy(address_string + NUM_TRYTES_ADDRESS, checksum, sum_len);
  free(checksum);
  checksum = NULL;
  address_string[NUM_TRYTES_ADDRESS + sum_len] = '\0';
  bool ok = qrcodegen_encodeText((char *)address_string, tempBuffer, qr0, qrcodegen_Ecc_MEDIUM, qr_version, qr_version,
                                 qrcodegen_Mask_AUTO, true);
#else
  bool ok = qrcodegen_encodeText(CONFIG_IOTA_RECEIVER, tempBuffer, qr0, qrcodegen_Ecc_MEDIUM, qr_version, qr_version,
                                 qrcodegen_Mask_AUTO, true);
#endif

  if (ok) {
    int size = qrcodegen_getSize(qr0);
    for (int y = 0; y < size; y++) {
      for (int x = 0; x < size; x++) {
        if (qrcodegen_getModule(qr0, x, y)) {
          st7735_rect(x * element_size + offset_x, y * element_size + offset_y, element_size, element_size,
                      COLOR_BLACK);
        } else {
          st7735_rect(x * element_size + offset_x, y * element_size + offset_y, element_size, element_size,
                      COLOR_WHITE);
        }
      }
    }
  }
#endif
}

//===========End of LCD and QR code=========
