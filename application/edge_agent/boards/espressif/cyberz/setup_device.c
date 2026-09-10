/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "setup_device";

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                     const esp_lcd_panel_dev_config_t *panel_dev_config,
                                     esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t panel_dev_cfg = {0};
    memcpy(&panel_dev_cfg, panel_dev_config, sizeof(esp_lcd_panel_dev_config_t));

    esp_err_t ret = esp_lcd_new_panel_st7789(io, &panel_dev_cfg, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New st7789/st7735s panel failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // ST7735S-specific initialization sequence
    const uint8_t cmd_swreset[] = {0x01};  // Software reset
    const uint8_t cmd_slpout[]  = {0x11};  // Sleep out
    const uint8_t cmd_pixfmt[]  = {0x3A};  // Pixel format
    const uint8_t pixfmt_data[] = {0x05};  // RGB565 (2 bytes per pixel)
    const uint8_t cmd_madctl[]  = {0x36};  // Memory access control
    const uint8_t cmd_disp_on[] = {0x29};  // Display on

    ret = esp_lcd_panel_io_tx_param(io, cmd_swreset[0], NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send SWRESET failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    ret = esp_lcd_panel_io_tx_param(io, cmd_slpout[0], NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send SLPOUT failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    vTaskDelay(pdMS_TO_TICKS(120));

    ret = esp_lcd_panel_io_tx_param(io, cmd_pixfmt[0], pixfmt_data, sizeof(pixfmt_data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set pixel format failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ret = esp_lcd_panel_io_tx_param(io, cmd_madctl[0], NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set MADCTL failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ret = esp_lcd_panel_io_tx_param(io, cmd_disp_on[0], NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display on failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    ESP_LOGI(TAG, "ST7735S panel initialized");
    return ESP_OK;

fail:
    esp_lcd_panel_del(*ret_panel);
    *ret_panel = NULL;
    return ret;
}
