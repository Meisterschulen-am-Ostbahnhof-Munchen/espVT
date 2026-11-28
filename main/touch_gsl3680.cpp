#include "touch.hpp"

#include "board_pins.hpp"
#include "board_support.hpp"
#include "isobus_terminal_server.hpp"
#include "lvgl_terminal_view.hpp"

#include <algorithm>
#include <cstdint>

#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// esp_lcd_touch_gsl3680.h defines the controller firmware as a static array,
// so only the factory function is declared here.
extern "C" esp_err_t esp_lcd_touch_new_i2c_gsl3680(esp_lcd_panel_io_handle_t io,
                                                   const esp_lcd_touch_config_t *config,
                                                   esp_lcd_touch_handle_t *out_touch);

namespace
{
constexpr char TAG[] = "gsl3680";
constexpr std::uint8_t I2C_ADDRESS = 0x40;

esp_lcd_touch_handle_t controller = nullptr;
TaskHandle_t task_handle = nullptr;

// The controller reports portrait coordinates with X mirrored against the
// panel. Map them to the landscape surface the same way the PPA rotates frames.
void to_lcd(std::uint16_t raw_x, std::uint16_t raw_y, std::uint16_t &x, std::uint16_t &y)
{
    const int portrait_x = std::clamp<int>(raw_x, 0, board::PANEL_NATIVE_WIDTH - 1);
    const int portrait_y = std::clamp<int>(raw_y, 0, board::PANEL_NATIVE_HEIGHT - 1);
#if CONFIG_ISOVT_DISPLAY_ROTATION_270
    x = static_cast<std::uint16_t>(portrait_y);
    y = static_cast<std::uint16_t>(portrait_x);
#else
    x = static_cast<std::uint16_t>(board::LCD_WIDTH - 1 - portrait_y);
    y = static_cast<std::uint16_t>(board::LCD_HEIGHT - 1 - portrait_x);
#endif
}

// ISO 11783-6 4.6.18: without simultaneous activation (Get Hardware byte 4 bits 4
// and 5 are 0) only one Button or Key is active at a time, and another touch
// while it is held is ignored. So the first finger is followed by its controller
// ID: lifting it releases at once, even while other fingers stay, and a new
// press needs all fingers lifted.
void touch_task(void *)
{
    enum class State
    {
        Idle,
        Tracking,
        WaitForAllLifted,
    };
    State state = State::Idle;
    std::uint16_t tracked_finger = 0;
    esp_err_t last_error = ESP_OK;

    while (true)
    {
        esp_lcd_touch_point_data_t points[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {};
        std::uint8_t count = 0;
        esp_err_t result = esp_lcd_touch_read_data(controller);
        if (result == ESP_OK)
        {
            result = esp_lcd_touch_get_data(controller, points, &count, CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
        }
        if (result != ESP_OK)
        {
            if (result != last_error)
            {
                ESP_LOGW(TAG, "GSL3680 read failed: %s", esp_err_to_name(result));
            }
            last_error = result;
        }
        else
        {
            last_error = ESP_OK;
            switch (state)
            {
                case State::Idle:
                    if (count > 0)
                    {
                        std::uint16_t x = 0;
                        std::uint16_t y = 0;
                        to_lcd(points[0].x, points[0].y, x, y);
                        ESP_LOGI(TAG, "Touch raw=%u,%u lcd=%u,%u finger=%u", points[0].x, points[0].y, x, y,
                                 points[0].strength);
                        tracked_finger = points[0].strength;
                        state = State::Tracking;
                        if (!isovt::on_touch_pressed(x, y))
                        {
                            vt_view::on_touch(x, y, count);
                        }
                    }
                    break;

                case State::Tracking:
                {
                    bool still_down = false;
                    for (std::uint8_t index = 0; index < count; ++index)
                    {
                        still_down = still_down || (points[index].strength == tracked_finger);
                    }
                    if (!still_down)
                    {
                        isovt::on_touch_released();
                        state = (count > 0) ? State::WaitForAllLifted : State::Idle;
                    }
                    break;
                }

                case State::WaitForAllLifted:
                    if (count == 0)
                    {
                        state = State::Idle;
                    }
                    break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t initialize_controller()
{
    ESP_RETURN_ON_ERROR(board::init_i2c(), TAG, "I2C initialization failed");

    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = I2C_ADDRESS;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 8;
    io_config.flags.disable_control_phase = 1;
    io_config.scl_speed_hz = 400000;
    esp_lcd_panel_io_handle_t io = nullptr;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(board::get_i2c_bus(), &io_config, &io), TAG, "GSL3680 I2C IO failed");

    esp_lcd_touch_config_t config = {};
    config.x_max = board::PANEL_NATIVE_WIDTH;
    config.y_max = board::PANEL_NATIVE_HEIGHT;
    config.rst_gpio_num = board::TOUCH_RESET;
    config.int_gpio_num = board::TOUCH_IRQ;
    config.levels.reset = 0;
    config.levels.interrupt = 0;
    return esp_lcd_touch_new_i2c_gsl3680(io, &config, &controller);
}
}

namespace touch
{
esp_err_t start()
{
    if (task_handle != nullptr)
    {
        return ESP_OK;
    }

    const esp_err_t result = initialize_controller();
    if (result != ESP_OK)
    {
        return result;
    }

    const BaseType_t task_result = xTaskCreatePinnedToCore(touch_task, "gsl3680", touch::TASK_STACK_SIZE, nullptr, 4, &task_handle, 0);
    if (task_result != pdPASS)
    {
        task_handle = nullptr;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "GSL3680 touch ready");
    return ESP_OK;
}

bool is_ready()
{
    return (controller != nullptr) && (task_handle != nullptr);
}
}
