// TFT_eSPI setup for the ESP32-2432S024C (2.4" ILI9341 on HSPI).
//
// Arduino IDE / arduino-cli: replace Arduino/libraries/TFT_eSPI/User_Setup.h with this file.
// PlatformIO passes the same values as build flags (see platformio.ini).

#define USER_SETUP_INFO "ESP32-2432S024C"
#define ILI9341_2_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   27  // the sketch switches the backlight on itself

#define USE_HSPI_PORT

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY       55000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
