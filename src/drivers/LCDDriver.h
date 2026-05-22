#pragma once
#include "controller.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

// LCD Display Update Intervals (in ms)
#define TASK_REFRESH_LCD 100

// Helper to set a temporary message (call from any task)
void LCD_setMessage(const char *msg);

void LCDInit();
void draw_menu();
bool getTouchInput(uint16_t *x, uint16_t *y);