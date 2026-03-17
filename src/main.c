/**
 * Copyright (C) 2022 by Mahyar Koshkouei <mk@deltabeard.com>
 * Copyright (C) 2024 by Vlastimil Slintak <slintak@uart.cz>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * PocketPico - Game Boy Emulator for Raspberry Pi Pico
 *
 * This file contains the main program for the PocketPico Game Boy emulator.
 * It handles ROM loading, display rendering, input processing, save file management,
 * and audio playback using the Peanut-GB emulator core.
 */

#include "config.h"
#include LCD_INCLUDE
#include "shared.h"
#include "ram_cart.h"
#include "flash.h"
#include "state.h"
#include "rom.h"
#include "gb.h"
#include "audio.h"
#include "sdcard.h"
#include "i2ckbd.h"
#include "gbcolors.h"

#define PEANUT_GB_HEADER_ONLY
#ifndef PEANUT_GB_H
#include "peanut_gb.h"
#endif

/* Standard C Headers */
#include <stdlib.h>
#include <string.h>

/* Raspberry Pi Pico SDK Headers */
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/spi.h>
#include <hardware/sync.h>
#include <hardware/timer.h>
#include <hardware/vreg.h>
#include <pico/bootrom.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <sys/unistd.h>
#include <hardware/irq.h>

#include "boot/picobin.h"
#include "hardware/watchdog.h"

struct minigb_apu_ctx apu_ctx = {0}; // Game Boy APU context
const uint8_t *rom = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
unsigned char rom_bank0[32768];         // 32KB buffer for ROM bank 0 (cached in RAM for faster access)
uint8_t ram[32768];                     // 32KB buffer for cartridge RAM
uint8_t pixels_buffer[WIDTH * 2] = {0}; // Line buffer for rendering Game Boy LCD output
palette_t palette;                      // Current color palette
uint8_t manual_palette_selected = 0;    // Index of manually selected palette
int lcd_line_busy = 0;                  // Flag for LCD line rendering status

/**
 * Previous Joypad State
 *
 * Stores the previous state of all joypad buttons to detect button press events.
 * Each field is a 1-bit flag indicating whether the button was pressed.
 */
static struct
{
    unsigned a : 1;      // A button
    unsigned b : 1;      // B button
    unsigned select : 1; // Select button
    unsigned start : 1;  // Start button
    unsigned right : 1;  // Right direction
    unsigned left : 1;   // Left direction
    unsigned up : 1;     // Up direction
    unsigned down : 1;   // Down direction
} prev_joypad_bits;

#if ENABLE_LCD

/**
 * LCD Draw Line Callback
 *
 * Callback function used by the Game Boy emulator to draw a line of the LCD.
 * This function converts Game Boy pixels to RGB565 format and sends them to the display.
 * Each pixel is duplicated horizontally and vertically to scale the 160x144 Game Boy
 * screen to 320x288 pixels on the physical display.
 *
 * @param gb Pointer to the Game Boy emulator context
 * @param pixels Array of pixels for the current line
 * @param line Line number to draw
 */
void lcd_draw_line(struct gb_s *gb, const uint8_t pixels[LCD_WIDTH],
                   const uint_fast8_t line)
{
    // Duplicate each pixel horizontally (160 -> 320 pixels)
#if PEANUT_FULL_GBC_SUPPORT
    if (gb->cgb.cgbMode)
    {
        for (unsigned int x = 0; x < LCD_WIDTH; x++)
        {
            // Convert RGB555 to RGB565 properly
            uint16_t color555 = gb->cgb.fixPalette[pixels[x]];
            uint16_t r = (color555 >> 10) & 0x1F;
            uint16_t g = (color555 >> 5) & 0x1F;
            uint16_t b = color555 & 0x1F;
            uint16_t pixel = (r << 11) | ((g << 1) << 5) | b;
            // Duplicate each pixel twice in the buffer with correct byte order
            pixels_buffer[x * 4] = (uint8_t)(pixel >> 8);       // high byte of first pixel
            pixels_buffer[x * 4 + 1] = (uint8_t)(pixel & 0xFF); // low byte of first pixel
            pixels_buffer[x * 4 + 2] = (uint8_t)(pixel >> 8);   // high byte of second pixel
            pixels_buffer[x * 4 + 3] = (uint8_t)(pixel & 0xFF); // low byte of second pixel
        }
    }
    else
    {
#endif
        for (unsigned int x = 0; x < LCD_WIDTH; x++)
        {
            // Duplicate each pixel twice in the buffer with correct byte order
            uint16_t pixel = palette[(pixels[x] & LCD_PALETTE_ALL) >> 4][pixels[x] & 3];
            pixels_buffer[x * 4] = (uint8_t)(pixel >> 8);       // high byte of first pixel
            pixels_buffer[x * 4 + 1] = (uint8_t)(pixel & 0xFF); // low byte of first pixel
            pixels_buffer[x * 4 + 2] = (uint8_t)(pixel >> 8);   // high byte of second pixel
            pixels_buffer[x * 4 + 3] = (uint8_t)(pixel & 0xFF); // low byte of second pixel
        }
#if PEANUT_FULL_GBC_SUPPORT
    }
#endif

    LCD_BLIT(pixels_buffer, line, LCD_WIDTH, LCD_HEIGHT);
}
#endif

/**
 * Main Function
 *
 * Entry point of the program. Initializes hardware, sets up the Game Boy
 * emulator, and runs the main application loop.
 *
 * @return Should never return (runs in an infinite loop)
 */
int main(void)
{
    static struct gb_s gb;              // Game Boy emulator context
    enum gb_init_error_e ret;           // Initialization error code
    const int buf_words = (16 * 4) + 1; // Maximum of 16 partitions, each with maximum of 4 words returned, plus 1
    uint32_t *buffer = malloc(buf_words * 4);

    /* Initialize system hardware */
    vreg_set_voltage(VREG_VOLT);                  // Set voltage for overclocking
    sleep_ms(100);                                // Wait for voltage to stabilize
    set_sys_clock_khz(SYS_CLK_FREQ / 1000, true); // Overclock

    stdio_init_all();   // Initialize standard I/O
    DBG_INIT();         // Initialize debug output
    DBG_INFO("INIT: "); // Print initialization message

    /* Initialize subsystems */
#if ENABLE_SOUND
    audio_commands_e q_audio = AUDIO_CMD_IDLE;
    queue_init(&call_queue, sizeof(audio_commands_e), 1); // Initialize communication queue
    multicore_launch_core1(audio_process);                // Start audio processing on core 1
#endif

#if ENABLE_LCD
    init(SYS_CLK_FREQ); // Initialize display with system clock frequency
    start_game();       // Initialize display for game rendering
#endif

    init_i2c_kbd(); // Initialize I2C keyboard interface
    device_init();  // Initialize PocketPico device hardware

    while (true)
    {
#if ENABLE_SDCARD
        /* ROM File selector */
        rom_file_selector();
#endif

#if ENABLE_LCD
        set_spi_speed(SYS_CLK_FREQ / 4);
#ifdef LCD_CLEAR
        LCD_CLEAR();
#endif
#endif
        /* Initialize Game Boy emulator */
        memcpy(rom_bank0, rom, sizeof(rom_bank0)); // Copy ROM bank 0 to RAM for faster access
        ret = gb_init(&gb,                         // Initialize Game Boy context
                      &gb_rom_read,                // ROM read callback
                      &gb_cart_ram_read,           // RAM read callback
                      &gb_cart_ram_write,          // RAM write callback
                      &gb_error,                   // Error handling callback
                      NULL);                       // No custom context
        DBG_INFO("GB ");

        if (ret != GB_INIT_NO_ERROR)
        {
            DBG_INFO("Error: %d\n", ret);
            goto out;
        }

#if ENABLE_SDCARD
        /* Load saved emulator state */
        read_gb_emulator_state(&gb); // Try to load last saved emulator state
#endif

        /* Set up display colors */
        char rom_title[16];
        auto_assign_palette(palette, // Automatically assign a color palette
                            gb_colour_hash(&gb),
                            gb_get_rom_name(&gb, rom_title));

#if ENABLE_LCD
        gb_init_lcd(&gb, &lcd_draw_line); // Initialize LCD with draw line callback
        DBG_INFO("LCD ");
#endif

        DBG_INFO("\n> ");
        uint_fast32_t frames = 0;
        uint64_t start_time = time_us_64();
        while (1)
        {
            int input;

            /* Execute CPU cycles until the screen has to be redrawn. */
            gb_run_frame(&gb);
            frames++;

#if ENABLE_SOUND
            if (!gb.direct.frame_skip)
            {
                q_audio = AUDIO_CMD_PLAYBACK;
                queue_add_blocking(&call_queue, &q_audio);
            }
#endif
            /* Update buttons state */
            prev_joypad_bits.up = gb.direct.joypad_bits.up;
            prev_joypad_bits.down = gb.direct.joypad_bits.down;
            prev_joypad_bits.left = gb.direct.joypad_bits.left;
            prev_joypad_bits.right = gb.direct.joypad_bits.right;
            prev_joypad_bits.a = gb.direct.joypad_bits.a;
            prev_joypad_bits.b = gb.direct.joypad_bits.b;
            prev_joypad_bits.select = gb.direct.joypad_bits.select;
            prev_joypad_bits.start = gb.direct.joypad_bits.start;
            gb.direct.joypad_bits.up = input_pins[KEY_UP] == 0 ? 1 : 0;
            gb.direct.joypad_bits.down = input_pins[KEY_DOWN] == 0 ? 1 : 0;
            gb.direct.joypad_bits.left = input_pins[KEY_LEFT] == 0 ? 1 : 0;
            gb.direct.joypad_bits.right = input_pins[KEY_RIGHT] == 0 ? 1 : 0;
            gb.direct.joypad_bits.a = input_pins[KEY_A] == 0 ? 1 : 0;
            gb.direct.joypad_bits.b = input_pins[KEY_B] == 0 ? 1 : 0;
            gb.direct.joypad_bits.select = input_pins[KEY_SELECT] == 0 ? 1 : 0;
            gb.direct.joypad_bits.start = input_pins[KEY_START] == 0 ? 1 : 0;

            /* hotkeys (select + * combo)*/
            if (!gb.direct.joypad_bits.select)
            {
#if ENABLE_SOUND
                if (!gb.direct.joypad_bits.up && prev_joypad_bits.up)
                {
                    /* select + up: increase sound volume */
                    q_audio = AUDIO_CMD_VOLUME_UP;
                    queue_add_blocking(&call_queue, &q_audio);
                }
                if (!gb.direct.joypad_bits.down && prev_joypad_bits.down)
                {
                    /* select + down: decrease sound volume */
                    q_audio = AUDIO_CMD_VOLUME_DOWN;
                    queue_add_blocking(&call_queue, &q_audio);
                }
#endif
                if (!gb.direct.joypad_bits.right && prev_joypad_bits.right)
                {
                    /* select + right: select the next manual color palette */
                    if (manual_palette_selected < 12)
                    {
                        manual_palette_selected++;
                        manual_assign_palette(palette, manual_palette_selected);
                    }
                }
                if (!gb.direct.joypad_bits.left && prev_joypad_bits.left)
                {
                    /* select + left: select the previous manual color palette */
                    if (manual_palette_selected > 0)
                    {
                        manual_palette_selected--;
                        manual_assign_palette(palette, manual_palette_selected);
                    }
                }
                if (!gb.direct.joypad_bits.start && prev_joypad_bits.start)
                {
                    /* select + start: save ram and resets to the game selection menu */
#if ENABLE_SDCARD
                    write_cart_ram_file(&gb);
                    /* Try to save the emulator state for this game. */
                    write_gb_emulator_state(&gb);
#endif
                    goto out;
                }
                if (!gb.direct.joypad_bits.a && prev_joypad_bits.a)
                {
                    /* select + A: enable/disable frame-skip => fast-forward */
                    gb.direct.frame_skip = !gb.direct.frame_skip;
                    DBG_INFO("I gb.direct.frame_skip = %d\n", gb.direct.frame_skip);
                }
            }

#if ENABLE_DEBUG
            /* Serial monitor commands */
            input = getchar_timeout_us(0);
            if (input == PICO_ERROR_TIMEOUT)
                continue;

            switch (input)
            {
            case 'i':
                gb.direct.interlace = !gb.direct.interlace;
                break;

            case 'f':
                gb.direct.frame_skip = !gb.direct.frame_skip;
                break;

            case 'b':
            {
                uint64_t end_time;
                uint32_t diff;
                uint32_t fps;

                end_time = time_us_64();
                diff = end_time - start_time;
                fps = ((uint64_t)frames * 1000 * 1000) / diff;
                DBG_INFO("Frames: %u\n"
                         "Time: %lu us\n"
                         "FPS: %lu\n",
                         frames, diff, fps);
                stdio_flush();
                frames = 0;
                start_time = time_us_64();
                break;
            }

            case '\n':
            case '\r':
            {
                gb.direct.joypad_bits.start = 0;
                break;
            }

            case '\b':
            {
                gb.direct.joypad_bits.select = 0;
                break;
            }

            case '8':
            {
                gb.direct.joypad_bits.up = 0;
                break;
            }

            case '2':
            {
                gb.direct.joypad_bits.down = 0;
                break;
            }

            case '4':
            {
                gb.direct.joypad_bits.left = 0;
                break;
            }

            case '6':
            {
                gb.direct.joypad_bits.right = 0;
                break;
            }

            case 'z':
            case 'w':
            {
                gb.direct.joypad_bits.a = 0;
                break;
            }

            case 'x':
            {
                gb.direct.joypad_bits.b = 0;
                break;
            }

            case 'q':
                goto out;

            default:
                break;
            }
#endif /* ENABLE_DEBUG */
        }

    out:
        DBG_INFO("\nEmulation Ended");
    }
}
