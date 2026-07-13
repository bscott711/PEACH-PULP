#pragma once

// Milestone-1 placeholder: message mailbox only, no display yet.
// Milestone 2 replaces this with the full U8g2/OLED-based driver ported
// from PEACH_STEM (draw_menu, splash/OTA screens, etc.).

// Helper to set a temporary message (call from any task)
void LCD_setMessage(const char *msg);

// Helper to notify LCD of a button press (for visual flash)
void LCD_notifyButtonPress(int index);
