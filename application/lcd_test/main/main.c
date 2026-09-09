/*
 * ST7789P3 display test for ESP32-S3 — bypasses ESP-Claw entirely.
 *
 * Wiring (user's fixed setup):
 *   SCLK -> GPIO4, MOSI -> GPIO5, CS -> GPIO15, DC -> GPIO7,
 *   RST  -> GPIO6, BL   -> 3V3 (backlight is powered directly, not by GPIO)
 *
 * The screen is an ST7789P3 (240x320 portrait, RGB565). This test sends the
 * vendor-specific ST7789P3 init sequence directly (NOT the generic ST7789
 * driver), because the standard init leaves the P3 panel in a state where it
 * shows a white screen. Solid-color loop, one row at a time (small buffer),
 * every step logged over UART (115200).
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

static const char *TAG = "LCD_P3";

#define PIN_SCLK      4
#define PIN_MOSI      5
#define PIN_CS        15
#define PIN_DC        7
#define PIN_RST       6

#define W_RES         240   /* columns (native portrait) */
#define H_RES         320   /* rows    */

static esp_lcd_panel_io_handle_t s_io = NULL;
static uint16_t s_row[240]; /* one row of RGB565 */

/* ---- low-level: send one command (DC=0) then optional params (DC=1) ---- */
static esp_err_t tx_cmd(esp_lcd_panel_io_handle_t io, uint8_t cmd,
                        const uint8_t *param, size_t len)
{
    return esp_lcd_panel_io_tx_param(io, cmd, param, len);
}

static esp_err_t tx_byte(esp_lcd_panel_io_handle_t io, uint8_t cmd, uint8_t d)
{
    return tx_cmd(io, cmd, &d, 1);
}

/* ---- vendor ST7789P3 init sequence (from the screen's datasheet) ---- */
static esp_err_t st7789p3_init(esp_lcd_panel_io_handle_t io)
{
    uint8_t d;
    uint8_t b2[]  = {0x0C, 0x0C, 0x00, 0x33, 0x33};           /* Porch Setting   */
    uint8_t d0b[] = {0xA4, 0xA1};                              /* VRVS            */
    uint8_t e0[]  = {0xF0,0x03,0x09,0x0B,0x0A,0x16,0x2B,0x33,
                     0x41,0x38,0x14,0x14,0x29,0x2F};           /* Gamma +       */
    uint8_t e1[]  = {0xF0,0x04,0x06,0x09,0x08,0x04,0x2B,0x32,
                     0x41,0x36,0x12,0x12,0x2A,0x30};           /* Gamma -       */
    uint8_t col[] = {0x00, 0x00, 0x00, 0xEF};                  /* Column 0..239  */
    uint8_t row[] = {0x00, 0x00, 0x01, 0x3F};                  /* Row 0..319     */

    ESP_RETURN_ON_ERROR(tx_cmd(io, 0xB2, b2, 5),   TAG, "0xB2");
    ESP_RETURN_ON_ERROR(tx_byte(io, 0x36, 0x00),   TAG, "0x36"); /* MADCTL */
    ESP_RETURN_ON_ERROR(tx_byte(io, 0x3A, 0x55),   TAG, "0x3A"); /* 16-bit RGB565 */
    ESP_RETURN_ON_ERROR(tx_byte(io, 0xB7, 0x55),   TAG, "0xB7");
    ESP_RETURN_ON_ERROR(tx_byte(io, 0xBB, 0x1A),   TAG, "0xBB");
    ESP_RETURN_ON_ERROR(tx_byte(io, 0xC0, 0x2C),   TAG, "0xC0");
    ESP_RETURN_ON_ERROR(tx_byte(io, 0xC2, 0x01),   TAG, "0xC2");
    ESP_RETURN_ON_ERROR(tx_byte(io, 0xC3, 0x19),   TAG, "0xC3");
    ESP_RETURN_ON_ERROR(tx_byte(io, 0xC6, 0x0F),   TAG, "0xC6");
    ESP_RETURN_ON_ERROR(tx_byte(io, 0xD0, 0xA7),   TAG, "0xD0");
    ESP_RETURN_ON_ERROR(tx_cmd(io, 0xD0, d0b, 2),  TAG, "0xD0b");
    ESP_RETURN_ON_ERROR(tx_byte(io, 0xD6, 0xA1),   TAG, "0xD6");
    ESP_RETURN_ON_ERROR(tx_cmd(io, 0xE0, e0, 14),  TAG, "0xE0");
    ESP_RETURN_ON_ERROR(tx_cmd(io, 0xE1, e1, 14),  TAG, "0xE1");

    ESP_RETURN_ON_ERROR(tx_cmd(io, 0x21, NULL, 0), TAG, "0x21"); /* inversion on */
    ESP_RETURN_ON_ERROR(tx_cmd(io, 0x2A, col, 4),  TAG, "0x2A");
    ESP_RETURN_ON_ERROR(tx_cmd(io, 0x2B, row, 4),  TAG, "0x2B");

    ESP_RETURN_ON_ERROR(tx_cmd(io, 0x11, NULL, 0), TAG, "0x11"); /* sleep out */
    vTaskDelay(pdMS_TO_TICKS(120));
    ESP_RETURN_ON_ERROR(tx_cmd(io, 0x29, NULL, 0), TAG, "0x29"); /* display on  */
    return ESP_OK;
}

/* fill the whole window with a solid color, one row at a time */
static esp_err_t fill_solid(esp_lcd_panel_io_handle_t io, uint16_t color)
{
    uint8_t col[] = {0x00, 0x00, 0x00, 0xEF};
    for (int p = 0; p < W_RES; p++) s_row[p] = color;
    ESP_RETURN_ON_ERROR(tx_cmd(io, 0x2A, col, 4), TAG, "fill 0x2A");
    for (int y = 0; y < H_RES; y++) {
        uint8_t r[4] = {0x00, 0x00, (uint8_t)(y >> 8), (uint8_t)(y & 0xFF)};
        ESP_RETURN_ON_ERROR(tx_cmd(io, 0x2B, r, 4), TAG, "fill 0x2B");
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_color(io, 0x2C, s_row, sizeof(s_row)),
                            TAG, "fill 0x2C");
    }
    return ESP_OK;
}

static void reset_panel(void)
{
    gpio_config_t gc = {
        .pin_bit_mask = 1ULL << PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&gc);
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static esp_err_t init_spi(void)
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
        .data_io_default_level = false,
        .max_transfer_sz = W_RES * sizeof(uint16_t), /* one row per DMA chunk */
        .flags = 0,
        .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
        .intr_flags = 0,
    };
    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPI3 already initialized, continuing");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "spi_bus_initialize");

    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .spi_mode = 0,
        .pclk_hz = 40000000,
        .trans_queue_depth = 4,
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .flags = {0},
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &s_io),
                        TAG, "panel io create");
    return ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_spi());
    reset_panel();
    ESP_ERROR_CHECK(st7789p3_init(s_io));
    ESP_LOGI(TAG, "ST7789P3 init done, display ON — color test starting");

    const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0xF81F, 0x07FF};
    const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "MAGENTA", "CYAN"};
    const int n = sizeof(colors) / sizeof(colors[0]);

    while (1) {
        for (int i = 0; i < n; i++) {
            esp_err_t ret = fill_solid(s_io, colors[i]);
            ESP_LOGI(TAG, ">> screen should be %s now %s <<",
                     names[i], ret == ESP_OK ? "" : "(FILL FAILED)");
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
}
