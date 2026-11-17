#include "board_support.hpp"

#include "board_pins.hpp"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace
{
constexpr char TAG[] = "board";

// The CH422G has no registers: each function is its own I2C address and takes
// a single data byte.
constexpr uint8_t CH422G_ADDR_MODE = 0x24;
constexpr uint8_t CH422G_ADDR_IO_OUT = 0x38;
constexpr uint8_t CH422G_MODE_IO_OUTPUT = 0x01;

// Touch reset, backlight, LCD reset and SD CS high, EXIO5 low. This is the state
// the Waveshare demo leaves the expander in. EXIO5 stays low so the native USB
// port remains usable; CAN has its own pins on this board.
constexpr uint8_t OUTPUT_IDLE = 0x1E;

i2c_master_bus_handle_t bus = nullptr;
i2c_master_dev_handle_t mode_device = nullptr;
i2c_master_dev_handle_t output_device = nullptr;
uint8_t output_shadow = OUTPUT_IDLE;

esp_err_t add_device(uint8_t address, i2c_master_dev_handle_t &device)
{
    i2c_device_config_t config = {};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = 400000;
    return i2c_master_bus_add_device(bus, &config, &device);
}

esp_err_t write_outputs(uint8_t value)
{
    output_shadow = value;
    return i2c_master_transmit(output_device, &output_shadow, 1, 100);
}
}

namespace board
{
esp_err_t init_i2c()
{
    if (bus != nullptr)
    {
        return ESP_OK;
    }

    i2c_master_bus_config_t config = {};
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.i2c_port = I2C_NUM_0;
    config.sda_io_num = I2C_SDA;
    config.scl_io_num = I2C_SCL;
    config.glitch_ignore_cnt = 7;
    config.flags.enable_internal_pullup = true;

    const esp_err_t result = i2c_new_master_bus(&config, &bus);
    if (result == ESP_OK)
    {
        ESP_LOGI(TAG, "I2C initialized on SDA=%d SCL=%d", I2C_SDA, I2C_SCL);
    }
    return result;
}

i2c_master_bus_handle_t get_i2c_bus()
{
    return bus;
}

esp_err_t init_io_expander()
{
    ESP_RETURN_ON_ERROR(init_i2c(), TAG, "I2C init failed");
    if (output_device != nullptr)
    {
        return ESP_OK;
    }

    // Hold the CAN transceiver recessive until TWAI takes over the pins. This
    // runs once at startup, so bus-off recovery cannot reconfigure a live pin.
    ESP_RETURN_ON_ERROR(gpio_reset_pin(CAN_TX), TAG, "CAN TX reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(CAN_TX, GPIO_MODE_OUTPUT), TAG, "CAN TX direction failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(CAN_TX, 1), TAG, "CAN TX recessive level failed");
    ESP_RETURN_ON_ERROR(gpio_reset_pin(CAN_RX), TAG, "CAN RX reset failed");
    ESP_RETURN_ON_ERROR(gpio_set_direction(CAN_RX, GPIO_MODE_INPUT), TAG, "CAN RX direction failed");

    ESP_RETURN_ON_ERROR(add_device(CH422G_ADDR_MODE, mode_device), TAG, "CH422G mode attach failed");
    ESP_RETURN_ON_ERROR(add_device(CH422G_ADDR_IO_OUT, output_device), TAG, "CH422G output attach failed");
    ESP_RETURN_ON_ERROR(i2c_master_transmit(mode_device, &CH422G_MODE_IO_OUTPUT, 1, 100), TAG, "CH422G mode write failed");
    ESP_RETURN_ON_ERROR(write_outputs(OUTPUT_IDLE), TAG, "CH422G output write failed");
    ESP_LOGI(TAG, "CH422G IO expander initialized");
    return ESP_OK;
}

esp_err_t set_io_expander_output(int pin, bool high)
{
    if ((pin < 0) || (pin > 7))
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(init_io_expander(), TAG, "IO expander init failed");

    const uint8_t mask = static_cast<uint8_t>(1U << pin);
    return write_outputs(high ? (output_shadow | mask) : (output_shadow & static_cast<uint8_t>(~mask)));
}

esp_err_t enable_can_mode()
{
    // CAN has dedicated pins and a dedicated transceiver on this board.
    return ESP_OK;
}

esp_err_t recover_can_mode()
{
    return enable_can_mode();
}

esp_err_t enable_lcd_power()
{
    ESP_RETURN_ON_ERROR(set_io_expander_output(EXIO_LCD_RESET, false), TAG, "LCD reset assert failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(set_io_expander_output(EXIO_LCD_RESET, true), TAG, "LCD reset release failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    return set_io_expander_output(EXIO_LCD_BACKLIGHT, true);
}
}
