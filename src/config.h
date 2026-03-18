#pragma once

// Peanut-GB emulator settings
#define ENABLE_DEBUG 0                // Enable debug output
#define ENABLE_SOUND 1                // Enable sound output
#define ENABLE_SDCARD 1               // Enable SD card for ROM and save storage
#define PEANUT_GB_HIGH_LCD_ACCURACY 1 // Use high accuracy LCD emulation
#define PEANUT_FULL_GBC_SUPPORT 1     // Enable full Game Boy Color support

// system configuration (266MHz for RP2040, 300MHz for RP2350)
#if PICO_RP2040
#define VREG_VOLT VREG_VOLTAGE_1_15
#define SYS_CLK_FREQ 266 * MHZ
#elif PICO_RP2350
#define VREG_VOLT VREG_VOLTAGE_1_30
#define SYS_CLK_FREQ 300 * MHZ
#endif

// buffer settings
#define BUFFER_RAM_SIZE 32768
#define BUFFER_ROM_SIZE 1048576
#define BUFFER_ROM_BANK0_SIZE 32768
#define BUFFER_INCLUDE "buffer.h"
#define BUFFER_RAM_INIT buffer_ram_init
#define BUFFER_RAM_BUFFER_READ buffer_ram_buffer_read
#define BUFFER_RAM_BUFFER_WRITE buffer_ram_buffer_write
#define BUFFER_ROM_INIT buffer_rom_init
#define BUFFER_ROM_BUFFER_READ buffer_rom_buffer_read
#define BUFFER_ROM_BUFFER_WRITE buffer_rom_buffer_write
#define BUFFER_ROM_BANK0_INIT buffer_rom_bank0_init
#define BUFFER_ROM_BANK0_READ buffer_rom_bank0_read
#define BUFFER_ROM_BANK0_WRITE buffer_rom_bank0_write
#define BUFFER_ROM_BANK0_FILL buffer_rom_bank0_fill

/* Audio Hardware Configuration */
#define AUDIO_DATA_PIN 26  // I2S data pin
#define AUDIO_CLOCK_PIN 27 // I2S clock pin
#define AUDIO_PWM_PIN 26   // PWM output pin (same as data pin)
#define SPK_PWM_FREQ 22050 // PWM frequency for audio output

/* Display Buffer Configuration */
#define FRAME_BUFF_WIDTH 320  // Width of frame buffer
#define FRAME_BUFF_HEIGHT 320 // Height of frame buffer

// lcd methods
#define LCD_INCLUDE "picocalc.h"
#define LCD_INIT picocalc_init // (void)
#define LCD_STRING lcd_string  // (uint16_t x, uint16_t y, const char *str, uint16_t color)
#define LCD_CLEAR lcd_clear    // (void)
#define LCD_BLIT lcd_blit      // (const uint8_t *pixels, uint16_t line)

// storage methods
#define SD_INCLUDE "sdcard.h"
#define SD_FILE_READ file_read                         // (const char *filename, uint8_t *buffer, size_t buffer_size) -> size_t
#define SD_FILE_SIZE file_size                         // (const char *filename) -> size_t
#define SD_FILE_WRITE file_write                       // (const char *filename, const uint8_t *buffer, size_t buffer_size) -> bool
#define SD_FILE_LIST file_list                         // (const char *pattern, char filenames[][256], uint16_t skip, uint16_t max_count) -> uint16_t
#define SD_FILE_OPEN file_open                         // (const char *filename) -> void *
#define SD_FILE_CLOSE file_close                       // (void *handle) -> void
#define SD_FILE_READ_FILE_CHUNK file_read_file_chunk   // (void *handle, uint8_t *buffer, size_t buffer_size) -> size_t
#define SD_FILE_WRITE_OPEN file_write_open             // (const char *filename) -> void *
#define SD_FILE_WRITE_FILE_CHUNK file_write_file_chunk // (void *handle, const uint8_t *data, size_t size) -> bool

// buttons
#define BUTTON_INCLUDE "picocalc.h"
#define BUTTON_UP input_pins[KEY_UP] == 0 ? 1 : 0
#define BUTTON_DOWN input_pins[KEY_DOWN] == 0 ? 1 : 0
#define BUTTON_LEFT input_pins[KEY_LEFT] == 0 ? 1 : 0
#define BUTTON_RIGHT input_pins[KEY_RIGHT] == 0 ? 1 : 0
#define BUTTON_A input_pins[KEY_A] == 0 ? 1 : 0
#define BUTTON_B input_pins[KEY_B] == 0 ? 1 : 0
#define BUTTON_SELECT input_pins[KEY_SELECT] == 0 ? 1 : 0
#define BUTTON_START input_pins[KEY_START] == 0 ? 1 : 0