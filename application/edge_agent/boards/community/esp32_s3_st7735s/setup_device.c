/*
 * ESP32-S3-DevKitC-1 + ST7735S 128x160 Display Board
 *
 * Pin mapping:
 *   SPI2_HOST: SCLK=GPIO5, MOSI=GPIO7
 *   Control:   CS=GPIO4, DC=GPIO8, RST=GPIO6
 *   Backlight: GPIO46 (LEDC PWM)
 *
 * The board manager's dev_display_lcd (sub_type: spi) requires the board to
 * provide lcd_panel_factory_entry_t(), which creates the panel with the
 * driver matching the "chip" field in board_devices.yaml (st7789 driver,
 * compatible with ST7735S).
 */

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

static const char *TAG = "ESP32_S3_ST7735S";

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t panel_dev_cfg = {0};
    memcpy(&panel_dev_cfg, panel_dev_config, sizeof(esp_lcd_panel_dev_config_t));
    int ret = esp_lcd_new_panel_st7789(io, &panel_dev_cfg, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New st7789 panel failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "ST7735S (st7789 driver) panel created");
    return ESP_OK;
}
