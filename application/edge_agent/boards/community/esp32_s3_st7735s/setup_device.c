/*
 * ESP32-S3-DevKitC-1 + ST7735S 128x160 Display Board
 *
 * Pin mapping:
 *   SPI2_HOST: SCLK=GPIO5, MOSI=GPIO7
 *   Control:   CS=GPIO4, DC=GPIO8, RST=GPIO6
 *   Backlight: GPIO46 (LEDC PWM)
 *
 * The display_lcd device (ST7789 driver, 128x160) is initialized by the
 * board manager from board YAML. No custom C setup is required for this
 * board: TGAM1 is driven from pure Lua over UART, and the display is a
 * standard board-manager device.
 */
