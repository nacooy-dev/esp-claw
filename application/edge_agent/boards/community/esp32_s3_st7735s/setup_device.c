/*
 * ESP32-S3-DevKitC-1 + ST7735S 128x160 Display Board Setup
 *
 * Pin mapping:
 *   SPI2_HOST: SCLK=GPIO5, MOSI=GPIO7
 *   Control:   CS=GPIO4, DC=GPIO8, RST=GPIO6
 *   Backlight: GPIO46 (LEDC PWM)
 *
 * Display: ST7735S 128x160 (portrait, RGB565)
 * Note: Using st7789 driver as it's compatible with ST7735S
 */

#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_board_manager_includes.h"
#include "gen_board_device_custom.h"

static const char *TAG = "ESP32_S3_ST7735S";

/* Display parameters */
#define LCD_H_RES 128
#define LCD_V_RES 160
#define LCD_PIXEL_CLK_HZ (16 * 1000 * 1000)

/* Pin definitions */
#define LCD_PIN_CS    4
#define LCD_PIN_DC    8
#define LCD_PIN_RST   6
#define LCD_PIN_BL    46

/* ST7735S Init Commands (from datasheet) */
typedef struct {
    uint8_t cmd;
    const uint8_t *data;
    uint8_t len;
    unsigned int delay_ms;
} lcd_init_cmd_t;

static const lcd_init_cmd_t s_st7735s_init_cmds[] = {
    // Software reset
    {0x01, (uint8_t[]) {0x00}, 1, 120},
    // Sleep out
    {0x11, (uint8_t[]) {0x00}, 1, 120},
    // Frame rate
    {0xB1, (uint8_t[]) {0x01, 0x2C, 0x2D}, 3, 0},
    // Frame rate (medium gray)
    {0xB2, (uint8_t[]) {0x01, 0x2C, 0x2D}, 3, 0},
    // Frame rate (full gray)
    {0xB3, (uint8_t[]) {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D}, 6, 0},
    // Column inversion
    {0xB4, (uint8_t[]) {0x07}, 1, 0},
    // Power control 1
    {0xC0, (uint8_t[]) {0xA2, 0x02, 0x84}, 3, 0},
    // Power control 2
    {0xC1, (uint8_t[]) {0xC5}, 1, 0},
    // VCOM control 1
    {0xC5, (uint8_t[]) {0x36, 0x36}, 2, 0},
    // VCOM control 2
    {0xC7, (uint8_t[]) {0xB3}, 1, 0},
    // Memory access control (portrait mode)
    {0x36, (uint8_t[]) {0x00}, 1, 0},
    // Pixel format (RGB565)
    {0x3A, (uint8_t[]) {0x05}, 1, 0},
    // Gamma curve
    {0x26, (uint8_t[]) {0x01}, 1, 0},
    // Gamma 1
    {0xE0, (uint8_t[]) {0x3F, 0x25, 0x1C, 0x1E, 0x20, 0x12, 0x2A, 0x90,
                         0x24, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00}, 15, 0},
    // Gamma 2
    {0xE1, (uint8_t[]) {0x20, 0x36, 0x25, 0x1D, 0x22, 0x15, 0x2E, 0x90,
                         0x24, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00}, 15, 0},
    // Memory access control - set orientation
    {0x36, (uint8_t[]) {0x00}, 1, 0},
    // Column address set
    {0x2A, (uint8_t[]) {0x00, 0x00, 0x00, 0x7F}, 4, 0},
    // Page address set
    {0x2B, (uint8_t[]) {0x00, 0x00, 0x00, 0xA0}, 4, 0},
    // Normal display mode on
    {0x13, (uint8_t[]) {0x00}, 1, 0},
    // Display on
    {0x29, (uint8_t[]) {0x00}, 1, 0},
};

typedef struct {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
} dev_display_lcd_handles_t;

static const dev_display_lcd_config_t s_lcd_config = {
    .name = "display_lcd",
    .chip = "st7789",
    .sub_type = "spi",
    .lcd_width = LCD_H_RES,
    .lcd_height = LCD_V_RES,
    .swap_xy = true,
    .mirror_x = false,
    .mirror_y = false,
    .need_reset = true,
    .invert_color = false,
    .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
    .data_endian = LCD_RGB_DATA_ENDIAN_BIG,
    .bits_per_pixel = 16,
};

static dev_display_lcd_handles_t s_lcd_handles;

static esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                            const esp_lcd_panel_dev_config_t *panel_dev_config,
                                            esp_lcd_panel_handle_t *ret_panel)
{
    ESP_RETURN_ON_FALSE(io && panel_dev_config && ret_panel, ESP_ERR_INVALID_ARG, TAG,
                        "invalid arguments");

    esp_err_t ret = esp_lcd_new_panel_st7789(io, panel_dev_config, ret_panel);
    ESP_RETURN_ON_ERROR(ret, TAG, "failed to create st7789 panel");

    /* Send ST7735S-specific init commands */
    for (size_t i = 0; i < sizeof(s_st7735s_init_cmds) / sizeof(s_st7735s_init_cmds[0]); i++) {
        const lcd_init_cmd_t *cmd = &s_st7735s_init_cmds[i];

        ret = esp_lcd_panel_io_tx_param(io, cmd->cmd, cmd->data, cmd->len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to send cmd 0x%02x: %s", cmd->cmd, esp_err_to_name(ret));
            esp_lcd_panel_del(*ret_panel);
            return ret;
        }

        if (cmd->delay_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(cmd->delay_ms));
        }
    }

    ESP_LOGI(TAG, "ST7735S panel initialized (%dx%d)", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

static int display_lcd_init(void *config, int cfg_size, void **device_handle)
{
    (void)config;
    (void)cfg_size;
    ESP_RETURN_ON_FALSE(device_handle != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "device_handle is NULL");

    esp_err_t ret;
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;

    /* Create SPI panel IO */
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .dc_gpio_num = LCD_PIN_DC,
        .spi_mode = 0,
        .pclk_hz = LCD_PIXEL_CLK_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };

    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle);
    ESP_RETURN_ON_ERROR(ret, TAG, "failed to create SPI panel IO");

    /* Create panel */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
    };

    ret = lcd_panel_factory_entry_t(io_handle, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        esp_lcd_panel_io_del(io_handle);
        return ret;
    }

    /* Set window */
    ret = esp_lcd_panel_set_window(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Set window failed: %s", esp_err_to_name(ret));
    }

    /* Fill white */
    ret = esp_lcd_panel_fill(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, (esp_lcd_rgb_pixel_t){.red = 0xFF, .green = 0xFF, .blue = 0xFF});
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Fill white failed: %s", esp_err_to_name(ret));
    }

    /* Setup backlight */
    gpio_config_t bl_cfg = {
        .pin_bit_mask = BIT64(LCD_PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&bl_cfg);
    ESP_RETURN_ON_ERROR(ret, TAG, "backlight GPIO config failed");
    gpio_set_level(LCD_PIN_BL, 1);  /* Turn on backlight */

    s_lcd_handles.io_handle = io_handle;
    s_lcd_handles.panel_handle = panel_handle;

    /* Override board config with our parameters */
    esp_board_device_override_config("display_lcd", (void *)&s_lcd_config, sizeof(s_lcd_config));

    *device_handle = &s_lcd_handles;

    ESP_LOGI(TAG, "ST7735S display ready: %dx%d @ GPIO5/SCLK, GPIO7/MOSI, GPIO4/CS, GPIO8/DC, GPIO6/RST",
             LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

static int display_lcd_deinit(void *device_handle)
{
    dev_display_lcd_handles_t *handles = (dev_display_lcd_handles_t *)device_handle;
    if (handles != NULL) {
        if (handles->panel_handle != NULL) {
            esp_lcd_panel_del(handles->panel_handle);
            handles->panel_handle = NULL;
        }
        if (handles->io_handle != NULL) {
            esp_lcd_panel_io_del(handles->io_handle);
            handles->io_handle = NULL;
        }
    }
    gpio_set_level(LCD_PIN_BL, 0);  /* Turn off backlight */
    ESP_LOGI(TAG, "ST7735S display deinitialized");
    return ESP_OK;
}

CUSTOM_DEVICE_IMPLEMENT(display_lcd, display_lcd_init, display_lcd_deinit);
