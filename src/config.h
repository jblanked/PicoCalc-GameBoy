#pragma once

// Peanut-GB emulator settings
#define ENABLE_DEBUG 1                // Enable debug output
#define ENABLE_LCD 1                  // Enable LCD display output
#define ENABLE_SOUND 1                // Enable sound output
#define ENABLE_SDCARD 1               // Enable SD card for ROM and save storage
#define PEANUT_GB_HIGH_LCD_ACCURACY 1 // Use high accuracy LCD emulation
#define PEANUT_FULL_GBC_SUPPORT 0     // Disable full Game Boy Color support

// system configuration (266MHz for RP2040, 300MHz for RP2350)
#if PICO_RP2040
#define VREG_VOLT VREG_VOLTAGE_1_15
#define SYS_CLK_FREQ 266 * MHZ
#elif PICO_RP2350
#define VREG_VOLT VREG_VOLTAGE_1_30
#define SYS_CLK_FREQ 300 * MHZ
#endif

/* Audio Hardware Configuration */
#if ENABLE_SOUND
#define AUDIO_DATA_PIN 26  // I2S data pin
#define AUDIO_CLOCK_PIN 27 // I2S clock pin
#define AUDIO_PWM_PIN 26   // PWM output pin (same as data pin)
#define SPK_PWM_FREQ 22050 // PWM frequency for audio output
#endif

/* Display Buffer Configuration */
#define FRAME_BUFF_WIDTH 320  // Width of frame buffer
#define FRAME_BUFF_HEIGHT 320 // Height of frame buffer