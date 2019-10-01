#pragma once

#include "ST7735.h"
#include "cclient/api/core/core_api.h"
#include "cclient/api/extended/extended_api.h"

retcode_t init_iota_client();
uint64_t get_balance();
retcode_t update_receiver_address();

void lcd_init();
void lcd_fill_screen(int16_t color);
void lcd_print(int16_t x, int16_t y, int16_t color, const char *fmt, ...);
void lcd_draw_qrcode();

void show_balace(uint64_t balance);