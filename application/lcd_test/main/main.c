/*
 * Minimal ST7789 display test for ESP32-S3 — bypasses ESP-Claw entirely.
 *
 * Wiring (user's fixed setup):
 *   SCLK -> GPIO4, MOSI -> GPIO5, CS -> GPIO15, DC -> GPIO7,
 *   RST  -> GPIO6, BL   -> GPIO16
 *
 * Runs a solid-color loop (2s each) and logs every step over UART (115200).
 */

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"

static const char *TAG = "LCD_TEST";

#define PIN_SCLK      4
#define PIN_MOSI      5
#define PIN_CS        15
#define PIN_DC        7
#define PIN_RST       6
#define PIN_BL        16

#define H_RES         320
#define V_RES         240

static esp_lcd_panel_handle_t s_panel = NULL;
static esp_lcd_panel_io_handle_t s_io = NULL;

static esp_err_t init_backlight(void)
{
    gpio_config_t gc = {
        .pin_bit_mask = 1ULL << PIN_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&gc), TAG, "backlight gpio_config");
    gpio_set_level(PIN_BL, 1);
    ESP_LOGI(TAG, "backlight ON (GPIO%d)", PIN_BL);
    return ESP_OK;
}

static esp_err_t init_spi_bus(void)
{
    const spi_bus_config_t buscfg = {
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = H_RES * V_RES * sizeof(uint16_t),
        .flags = { .spi_mode = 0 },
    };
    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPI3 already initialized, continuing");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "spi_bus_initialize");
    ESP_LOGI(TAG, "SPI3 initialized: SCLK=GPIO%d MOSI=GPIO%d", PIN_SCLK, PIN_MOSI);
    return ESP_OK;
}

static esp_err_t init_panel(void)
{
    // Reset sequence
    gpio_set_direction(PIN_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_LOGI(TAG, "RST pulse sent (GPIO%d)", PIN_RST);

    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = 40000000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = { .use_dma = true },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &s_io), TAG, "panel io create");
    ESP_LOGI(TAG, "panel IO created: DC=GPIO%d CS=GPIO%d @40MHz", PIN_DC, PIN_CS);

    const esp_lcd_st7789_config_t panel_config = {
        .background_clean = false,
        .rgb_element_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .image_polarity_inverse = true,   // invert_color (matches breadboard)
        .mirror_x = false,
        .mirror_y = true,                 // matches breadboard
        .swap_xy = true,                  // matches breadboard
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_io, &panel_config, &s_panel), TAG, "st7789 create");
    ESP_LOGI(TAG, "ST7789 panel object created");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "display on");
    ESP_LOGI(TAG, "panel init + display ON — sending color test now");
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_backlight());
    ESP_ERROR_CHECK(init_spi_bus());
    ESP_ERROR_CHECK(init_panel());

    // RGB565 colors: red, green, blue, white, magenta, cyan
    const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0xF81F, 0x07FF};
    const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "MAGENTA", "CYAN"};
    const int n = sizeof(colors) / sizeof(colors[0]);

    while (1) {
        for (int i = 0; i < n; i++) {
            esp_err_t ret = esp_lcd_panel_fill_color(s_panel, 0, 0, H_RES, V_RES, colors[i]);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "fill_color %s FAILED: %s", names[i], esp_err_to_name(ret));
            } else {
                ESP_LOGI(TAG, ">> screen should be %s now <<", names[i]);
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}
