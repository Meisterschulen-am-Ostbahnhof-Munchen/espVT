#pragma once

#include <cstdint>

#include "esp_err.h"

// Display hardware behind lcd_panel. Frames are drawn into a landscape RGB565
// buffer of board::LCD_WIDTH x board::LCD_HEIGHT pixels and then presented.
namespace lcd_backend
{
esp_err_t init();
// Buffer for the frame being drawn; nullptr before init() succeeded.
std::uint16_t *draw_buffer();
// Shows the draw buffer and waits until it has been scanned out. Afterwards
// draw_buffer() may return a different buffer with older content.
esp_err_t present();
// True when draw_buffer() keeps the last presented frame and present_rect() works.
bool supports_partial_present();
// Shows one rectangle of the draw buffer (LCD pixels) without a full present.
esp_err_t present_rect(int x, int y, int width, int height);
}
