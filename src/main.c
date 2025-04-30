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

// Peanut-GB emulator settings
#define ENABLE_LCD 1
#define ENABLE_SOUND 1
#define ENABLE_SDCARD 1
#define PEANUT_GB_HIGH_LCD_ACCURACY 1
#define PEANUT_GB_USE_BIOS 0
#define PEANUT_FULL_GBC_SUPPORT 0
#define SYS_CLK_FREQ 300 * MHZ

#define ENABLE_DEBUG 1

// Display selection
#define USE_ILI9225 0
#define USE_ILI9488 1

/**
 * Reducing VSYNC calculation to lower multiple.
 * When setting a clock IRQ to DMG_CLOCK_FREQ_REDUCED, count to
 * SCREEN_REFRESH_CYCLES_REDUCED to obtain the time required each VSYNC.
 * DMG_CLOCK_FREQ_REDUCED = 2^18, and SCREEN_REFRESH_CYCLES_REDUCED = 4389.
 * Currently unused.
 */
#define VSYNC_REDUCTION_FACTOR 16u
#define SCREEN_REFRESH_CYCLES_REDUCED (SCREEN_REFRESH_CYCLES / VSYNC_REDUCTION_FACTOR)
#define DMG_CLOCK_FREQ_REDUCED (DMG_CLOCK_FREQ / VSYNC_REDUCTION_FACTOR)

/* C Headers */
#include <stdlib.h>
#include <string.h>

/* RP2040 Headers */
#include <hardware/pio.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/spi.h>
#include <hardware/sync.h>
#include <hardware/flash.h>
#include <hardware/timer.h>
#include <hardware/vreg.h>
#include <pico/bootrom.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <sys/unistd.h>
#include <hardware/irq.h>

/* Project headers */
//#include "pwm_audio.h"
#include "debug.h"
#include "hedley.h"
#include "minigb_apu.h"
#include "sdcard.h"

//#include "i2s.h"
#include "gbcolors.h"

/*
#include "ili9488_lcd.h"
#include "ili9488_font.h"
#define SCREEN_WIDTH ILI9488_SCREEN_WIDTH
#define SCREEN_HEIGHT ILI9488_SCREEN_HEIGHT
*/
#include "i2ckbd.h"
#include "picocalc.h"
#define FRAME_BUFF_WIDTH 240
#define FRAME_BUFF_STRIDE (FRAME_BUFF_WIDTH * 2)
#define FRAME_BUFF_HEIGHT 240

#if ENABLE_SOUND

typedef enum
{
    AUDIO_CMD_IDLE = 0,
    AUDIO_CMD_PLAYBACK,
    AUDIO_CMD_VOLUME_UP,
    AUDIO_CMD_VOLUME_DOWN,
    AUDIO_CMD_INVALID
} audio_commands_e;

#define audio_read(a)      audio_read(&apu_ctx, (a))
#define audio_write(a, v)  audio_write(&apu_ctx, (a), (v));

/**
 * Global variables for audio task
 * stream contains N=AUDIO_SAMPLES samples
 * each sample is 32 bits
 * 16 bits for the left channel + 16 bits for the right channel in stereo interleaved format)
 * This is intended to be played at AUDIO_SAMPLE_RATE Hz
 */
int16_t *stream;
struct minigb_apu_ctx apu_ctx = {0};

// PWM audio driver
#define AUDIO_DATA_PIN 26
#define AUDIO_CLOCK_PIN 27
#define AUDIO_PWM_PIN 26
#define PIN_SPEAKER 26
#define SPK_LATENCY 256
#define SPK_PWM_FREQ 22050

#include "audio.h"
#include "peanut_gb.h"
#undef audio_read
#undef audio_write
#else
#include "peanut_gb.h"
#endif

/** Definition of ROM data
 * We're going to erase and reprogram a region 1Mb from the start of the flash
 * Once done, we can access this at XIP_BASE + 1Mb.
 * Game Boy DMG ROM size ranges from 32768 bytes (e.g. Tetris) to 1,048,576 bytes (e.g. Pokemod Red)
 */
//#define FLASH_TARGET_OFFSET ((1024 * 1024) + (256 * 1024))
#define FLASH_TARGET_OFFSET (1024 * 1024)
const uint8_t *rom = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
static unsigned char rom_bank0[65536];

static uint8_t ram[32768];
static int lcd_line_busy = 0;
static palette_t palette; // Colour palette
static uint8_t manual_palette_selected = 0;

static struct
{
    unsigned a : 1;
    unsigned b : 1;
    unsigned select : 1;
    unsigned start : 1;
    unsigned right : 1;
    unsigned left : 1;
    unsigned up : 1;
    unsigned down : 1;
} prev_joypad_bits;

/* Pixel data is stored in here. */
static uint16_t pixels_buffer[FRAME_BUFF_STRIDE * 240];

/**
 * Returns a byte from the ROM file at the given address.
 */
uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
    (void)gb;
    if (addr < sizeof(rom_bank0))
        return rom_bank0[addr];

    return rom[addr];
}

/**
 * Returns a byte from the cartridge RAM at the given address.
 */
uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
    (void)gb;
    return ram[addr];
}

/**
 * Writes a given byte to the cartridge RAM at the given address.
 */
void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr,
                       const uint8_t val)
{
    ram[addr] = val;
}

/**
 * Ignore all errors.
 */
void gb_error(struct gb_s *gb, const enum gb_error_e gb_err, const uint16_t addr)
{
#if 1
    const char *gb_err_str[4] = {
        "UNKNOWN",
        "INVALID OPCODE",
        "INVALID READ",
        "INVALID WRITE"};
    DBG_INFO("Error %d occurred: %s at %04X\n.\n", gb_err, gb_err_str[gb_err], addr);
//  abort();
#endif
}

#if ENABLE_LCD
void draw_string(int x, int y, const char *str)
{
    draw_string_rgb565(
        pixels_buffer, FRAME_BUFF_STRIDE, FRAME_BUFF_WIDTH, FRAME_BUFF_HEIGHT,
        x, y, str, 0xffff);
}

void clear_frame_buff()
{
    for (int i = 0; i < FRAME_BUFF_STRIDE * FRAME_BUFF_HEIGHT; i++)
    {
        pixels_buffer[i] = 0;
    }
}

void clear_screen_buff()
{
    for (int i = 0; i < (WIDTH) * HEIGHT; i++)
    {
        pixels_buffer[i] = 0;
    }
}

void update_lcd()
{
    start_write_data((WIDTH - FRAME_BUFF_WIDTH) / 2, (HEIGHT - FRAME_BUFF_HEIGHT) / 2,
                     FRAME_BUFF_WIDTH, FRAME_BUFF_HEIGHT, pixels_buffer);
    finish_write_data(true);
}

void update_full_screen()
{
    start_write_data(0, 0, WIDTH, HEIGHT, pixels_buffer);
    finish_write_data(true);
}

void lcd_draw_line(struct gb_s *gb, const uint8_t pixels[LCD_WIDTH],
                   const uint_fast8_t line)
{
#if PEANUT_FULL_GBC_SUPPORT
    if (gb->cgb.cgbMode)
    {
        for (unsigned int x = 0; x < LCD_WIDTH; x++)
        {
            pixels_buffer[x] = gb->cgb.fixPalette[pixels[x]] << 1;
        }
    }
    else
    {
#endif
        for (unsigned int x = 0; x < LCD_WIDTH; x++)
        {
            pixels_buffer[x] = palette[(pixels[x] & LCD_PALETTE_ALL) >> 4][pixels[x] & 3];
        }
#if PEANUT_FULL_GBC_SUPPORT
    }
#endif
    
    finish_write_data(false);
    if(line==0){
        start_window((WIDTH - LCD_WIDTH) / 2, ((HEIGHT - LCD_HEIGHT) / 2), LCD_WIDTH, LCD_HEIGHT);
    } else if (line == LCD_HEIGHT) {
        finish_write_data(true);
    } else {
        write_data(pixels_buffer, LCD_WIDTH);
    }
}

void lcd_draw_line_bis(struct gb_s *gb, const uint8_t pixels[LCD_WIDTH],
                       const uint_fast8_t line)
{
    // Duplicate each pixel horizontally (160 -> 320 pixels)
#if PEANUT_FULL_GBC_SUPPORT
    if (gb->cgb.cgbMode)
    {
        for (unsigned int x = 0; x < LCD_WIDTH; x++)
        {
            // Duplicate each pixel twice in the buffer
            pixels_buffer[x * 2] = gb->cgb.fixPalette[pixels[x]] << 1;
            pixels_buffer[x * 2 + 1] = gb->cgb.fixPalette[pixels[x]] << 1;
        }
    }
    else
    {
#endif
        for (unsigned int x = 0; x < LCD_WIDTH; x++)
        {
            // Duplicate each pixel twice in the buffer
            pixels_buffer[x * 2] = palette[(pixels[x] & LCD_PALETTE_ALL) >> 4][pixels[x] & 3];
            pixels_buffer[x * 2 + 1] = palette[(pixels[x] & LCD_PALETTE_ALL) >> 4][pixels[x] & 3];
        }
#if PEANUT_FULL_GBC_SUPPORT
    }
#endif
    
    finish_write_data(false);
    if(line==0){
        // Double the width when starting the window
        start_window((WIDTH - (LCD_WIDTH * 2)) / 2, ((HEIGHT - LCD_HEIGHT) / 2), LCD_WIDTH * 2, LCD_HEIGHT*2);
    } else if (line == LCD_HEIGHT) {
        finish_write_data(true);
    } else {
        // Write double-width line twice to create vertical duplication
        write_data(pixels_buffer, LCD_WIDTH * 2);
        write_data(pixels_buffer, LCD_WIDTH * 2);
    }
}
#endif

#if ENABLE_SDCARD
/**
 * Load a save file from the SD card
 */
void read_cart_ram_file(struct gb_s *gb)
{
    char filename[16];
    uint_fast32_t save_size;
    UINT br;

    gb_get_rom_name(gb, filename);
    save_size = gb_get_save_size(gb);
    if (save_size > 0)
    {
        sd_card_t *pSD = sd_get_by_num(0);
        FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
        if (FR_OK != fr)
        {
            DBG_INFO("E f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
            return;
        }

        FIL fil;
        fr = f_open(&fil, filename, FA_READ);
        if (fr == FR_OK)
        {
            f_read(&fil, ram, f_size(&fil), &br);
        }
        else
        {
            DBG_INFO("E f_open(%s) error: %s (%d)\n", filename, FRESULT_str(fr), fr);
        }

        fr = f_close(&fil);
        if (fr != FR_OK)
        {
            DBG_INFO("E f_close error: %s (%d)\n", FRESULT_str(fr), fr);
        }
        f_unmount(pSD->pcName);
        DBG_INFO("I read_cart_ram_file(%s) COMPLETE (%lu bytes)\n", filename, save_size);
    }
    else
    {
        DBG_INFO("I read_cart_ram_file(%s) SKIPPED\n", filename);
    }
}

/**
 * Write a save file to the SD card
 */
void write_cart_ram_file(struct gb_s *gb)
{
    char filename[16];
    uint_fast32_t save_size;
    UINT bw;

    gb_get_rom_name(gb, filename);
    save_size = gb_get_save_size(gb);
    if (save_size > 0)
    {
        sd_card_t *sd = sd_get_by_num(0);
        FRESULT fr = f_mount(&sd->fatfs, sd->pcName, 1);
        if (FR_OK != fr)
        {
            DBG_INFO("E f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
            return;
        }

        FIL fil;
        fr = f_open(&fil, filename, FA_CREATE_ALWAYS | FA_WRITE);
        if (fr == FR_OK)
        {
            f_write(&fil, ram, save_size, &bw);
        }
        else
        {
            DBG_INFO("E f_open(%s) error: %s (%d)\n", filename, FRESULT_str(fr), fr);
        }

        fr = f_close(&fil);
        if (fr != FR_OK)
        {
            DBG_INFO("E f_close error: %s (%d)\n", FRESULT_str(fr), fr);
        }

        f_unmount(sd->pcName);
    }

    DBG_INFO("I write_cart_ram_file(%s) COMPLETE (%lu bytes)\n", filename, bw);
}

/**
 * Read a save file with internal GB enumalor state from the SD card.
 * This state will allow to resume game from the last run.
 */
void read_gb_emulator_state(struct gb_s *gb)
{
    char filename[16];
    char filename_state[32];
    UINT br = 0;
    FIL fil;

    sd_card_t *sd = sd_get_by_num(0);
    FRESULT fr = f_mount(&sd->fatfs, sd->pcName, 1);

    gb_get_rom_name(gb, filename);
    sprintf(filename_state, "%s_state.bin", filename);
    fr = f_open(&fil, filename_state, FA_READ);

    if (fr == FR_OK)
    {
        f_read(&fil, (uint8_t *)gb, sizeof(struct gb_s), &br);
    }
    else
    {
        DBG_INFO("W read_gb_emulator_state(%s): SKIPPED (no previous state)\n", filename_state);
        goto finish;
    }

    DBG_INFO("I read_gb_emulator_state(%s) COMPLETED (%lu bytes)\n", filename_state, br);

finish:
    fr = f_close(&fil);
    if (fr != FR_OK)
    {
        DBG_INFO("W f_close error: %s (%d)\n", FRESULT_str(fr), fr);
    }

    f_unmount(sd->pcName);
}

/**
 * Write a save file with internal GB enumalor state to the SD card.
 * When loaded, this state will allow to resume game from the last run.
 */
void write_gb_emulator_state(struct gb_s *gb)
{
    char filename[16];
    char filename_state[32];
    UINT bw;
    FIL fil;

    sd_card_t *sd = sd_get_by_num(0);
    FRESULT fr = f_mount(&sd->fatfs, sd->pcName, 1);

    gb_get_rom_name(gb, filename);
    sprintf(filename_state, "%s_state.bin", filename);
    fr = f_open(&fil, filename_state, FA_CREATE_ALWAYS | FA_WRITE);

    if (fr == FR_OK)
    {
        f_write(&fil, (uint8_t *)gb, sizeof(struct gb_s), &bw);
    }
    else
    {
        DBG_INFO("E write_gb_emulator_state(%s) FAILED (%s)\n", filename_state, FRESULT_str(fr));
        goto finish;
    }

    DBG_INFO("I write_gb_emulator_state(%s) COMPLETED (%lu bytes)\n", filename, bw);

finish:
    fr = f_close(&fil);
    if (fr != FR_OK)
    {
        DBG_INFO("E f_close error: %s (%d)\n", FRESULT_str(fr), fr);
    }
    f_unmount(sd->pcName);
}

/**
 * Load a .gb rom file in flash from the SD card
 */
void load_cart_rom_file(char *filename)
{
    UINT br;
    uint8_t buffer[FLASH_SECTOR_SIZE];
    bool mismatch = false;
    sd_card_t *pSD = sd_get_by_num(0);
    FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    if (FR_OK != fr)
    {
        DBG_INFO("E f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
        return;
    }
    FIL fil;
    fr = f_open(&fil, filename, FA_READ);
    if (fr == FR_OK)
    {
        uint32_t flash_target_offset = FLASH_TARGET_OFFSET;
        for (;;)
        {
            f_read(&fil, buffer, sizeof buffer, &br);
            if (br == 0)
                break; /* end of file */

            DBG_INFO("I Erasing target region...\n");
            flash_range_erase(flash_target_offset, FLASH_SECTOR_SIZE);
            DBG_INFO("I Programming target region...\n");
            flash_range_program(flash_target_offset, buffer, FLASH_SECTOR_SIZE);

            /* Read back target region and check programming */
            DBG_INFO("I Done. Reading back target region...\n");
            for (uint32_t i = 0; i < FLASH_SECTOR_SIZE; i++)
            {
                if (rom[flash_target_offset + i] != buffer[i])
                {
                    mismatch = true;
                }
            }

            /* Next sector */
            flash_target_offset += FLASH_SECTOR_SIZE;
        }
        if (mismatch)
        {
            DBG_INFO("I Programming successful!\n");
        }
        else
        {
            DBG_INFO("E Programming failed!\n");
        }
    }
    else
    {
        DBG_INFO("E f_open(%s) error: %s (%d)\n", filename, FRESULT_str(fr), fr);
    }

    fr = f_close(&fil);
    if (fr != FR_OK)
    {
        DBG_INFO("E f_close error: %s (%d)\n", FRESULT_str(fr), fr);
    }
    f_unmount(pSD->pcName);

    DBG_INFO("I load_cart_rom_file(%s) COMPLETE (%lu bytes)\n", filename, br);
}

/**
 * Function used by the rom file selector to display one page of .gb rom files
 */
uint16_t rom_file_selector_display_page(char filename[22][256], uint16_t num_page)
{
    sd_card_t *pSD = sd_get_by_num(0);
    DIR dj;
    FILINFO fno;
    FRESULT fr;

    fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    if (FR_OK != fr)
    {
        DBG_INFO("E f_mount error: %s (%d)\n", FRESULT_str(fr), fr);
        return 0;
    }

    /* clear the filenames array */
    for (uint8_t ifile = 0; ifile < 22; ifile++)
    {
        strcpy(filename[ifile], "");
    }

    /* search *.gb files */
    uint16_t num_file = 0;
    fr = f_findfirst(&dj, &fno, ".", "?*.gb");

    /* skip the first N pages */
    if (num_page > 0)
    {
        while (num_file < num_page * 22 && fr == FR_OK && fno.fname[0])
        {
            num_file++;
            fr = f_findnext(&dj, &fno);
        }
    }

    /* store the filenames of this page */
    num_file = 0;
    while (num_file < 22 && fr == FR_OK && fno.fname[0])
    {
        if (fno.fname[0] != '.')
        {
            /* Skip any file starting with dot. These are hidden files. */
            strcpy(filename[num_file], fno.fname);
            num_file++;
        }

        fr = f_findnext(&dj, &fno);
    }
    f_closedir(&dj);
    f_unmount(pSD->pcName);

    /* display *.gb rom files on screen */
    clear_frame_buff();
    for (uint8_t ifile = 0; ifile < num_file; ifile++)
    {
        DBG_INFO("Game: %s\n", filename[ifile]);
        draw_string(20, ifile * 20, filename[ifile]);
    }
    update_lcd();
    return num_file;
}

/**
 * The ROM selector displays pages of up to 22 rom files
 * allowing the user to select which rom file to start
 * Copy your *.gb rom files to the root directory of the SD card
 */
void rom_file_selector()
{
    DBG_INFO("ROM File Selector: Starting...\n");
    uint16_t num_page = 0;
    char filename[22][256];
    uint16_t num_file;
    char buf[6];
    bool break_outer = false;

    /* display the first page with up to 22 rom files */
    num_file = rom_file_selector_display_page(filename, num_page);
    DBG_INFO("ROM File Selector: Found %d files on first page\n", num_file);

    /* select the first rom */
    uint8_t selected = 0;
    DBG_INFO("ROM File Selector: Waiting 5 seconds before highlighting first ROM\n");

    DBG_INFO("ROM File Selector: Highlighting first ROM: %s\n", filename[selected]);
    sprintf(buf, "%02d", selected + 1);
    draw_string(0, FRAME_BUFF_HEIGHT - 20, buf);
    draw_string(0, (selected % 22) * 20, "=>");
    update_lcd();

    /* get user's input */
    bool up = true, down = true, left = true, right = true, a = true, b = true, select = true, start = true;
    while (true)
    {
        switch (wait_key())
        {
        case KEY_A:
        case KEY_B:
            DBG_INFO("ROM File Selector: A/B button pressed - loading ROM: %s\n", filename[selected]);
            /*
            rom_file_selector_display_page(filename, num_page);
            sprintf(buf, "%s", filename[selected]);
            draw_string(0, FRAME_BUFF_HEIGHT - 20, buf);
            update_lcd();
            sleep_ms(150);
            */
            load_cart_rom_file(filename[selected]);
            break_outer = true;
            break;

        case KEY_START:
            DBG_INFO("ROM File Selector: Start button pressed - resuming last game\n");
            break_outer = true;
            break;

        case KEY_UP:
            DBG_INFO("ROM File Selector: Up button - selecting previous ROM\n");
            rom_file_selector_display_page(filename, num_page);
            draw_string(0, (selected % 22) * 20, "");
            if (selected == 0)
            {
                selected = num_file - 1;
            }
            else
            {
                selected--;
            }
            DBG_INFO("ROM File Selector: Selected ROM: %s\n", filename[selected]);
            sprintf(buf, "%02d", selected + 1);
            draw_string(0, FRAME_BUFF_HEIGHT - 20, buf);
            draw_string(0, (selected % 22) * 20, "=>");
            update_lcd();
            sleep_ms(150);
            break;

        case KEY_DOWN:

            DBG_INFO("ROM File Selector: Down button - selecting next ROM\n");
            rom_file_selector_display_page(filename, num_page);
            selected++;
            if (selected >= num_file)
                selected = 0;
            DBG_INFO("ROM File Selector: Selected ROM: %s\n", filename[selected]);
            // ili9488_text(filename[selected], 0, selected*8, 0xFFFF, 0xF800);
            sprintf(buf, "%02d", selected + 1);
            draw_string(0, FRAME_BUFF_HEIGHT - 20, buf);
            draw_string(0, (selected % 22) * 20, "=>");
            update_lcd();
            sleep_ms(150);
            break;
        }

        if (break_outer)
            break;
    }

    DBG_INFO("ROM File Selector: Exiting selector\n");
}

#endif

#if ENABLE_SOUND

void core1_audio(void)
{
    /* Allocate memory for the stream buffer */
    stream = malloc(AUDIO_SAMPLES_TOTAL * sizeof(int16_t));
    assert(stream != NULL);
    memset(stream, 0, AUDIO_SAMPLES_TOTAL * sizeof(int16_t));

    /* Initialize I2S sound driver (using PIO0) */
    i2s_config_t i2s_config = i2s_get_default_config();
    i2s_config.sample_freq = AUDIO_SAMPLE_RATE;
    i2s_config.dma_trans_count = AUDIO_SAMPLES;
    i2s_volume(&i2s_config, 4);
    i2s_init(&i2s_config);

    /* Initialize audio emulation. */
    audio_init(&apu_ctx);

    DBG_INFO("I Audio ready on core1.\n");

    while (1)
    {
        audio_commands_e cmd = multicore_fifo_pop_blocking_inline();
        switch (cmd)
        {
        case AUDIO_CMD_PLAYBACK:
            audio_callback(&apu_ctx, stream);
            i2s_dma_write(&i2s_config, stream);
            break;

        case AUDIO_CMD_VOLUME_UP:
            i2s_increase_volume(&i2s_config);
            break;

        case AUDIO_CMD_VOLUME_DOWN:
            i2s_decrease_volume(&i2s_config);
            break;

        default:
            break;
        }
    }

    HEDLEY_UNREACHABLE();
}
#endif

int main(void)
{
    static struct gb_s gb;
    enum gb_init_error_e ret;
    
    /* Overclock to 300 MHZ. */
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(100);
    set_sys_clock_khz(SYS_CLK_FREQ / 1000, true);

    DBG_INIT();
    DBG_INFO("INIT: ");

#if ENABLE_SOUND    
    multicore_launch_core1(core1_audio);
#endif

#if ENABLE_LCD
    init(SYS_CLK_FREQ);
    start_game();
    clear_screen_buff();
    update_full_screen();
#endif

    init_i2c_kbd(); // Init keyboard
    device_init(); // Init device
    
    while (true)
    {

#if ENABLE_SDCARD
        /* ROM File selector */
        rom_file_selector();
#endif

#if ENABLE_LCD
    set_spi_speed(SYS_CLK_FREQ / 4);
    clear_frame_buff();
    update_lcd();
#endif
        /* Initialise GB context. */
        memcpy(rom_bank0, rom, sizeof(rom_bank0));
        ret = gb_init(&gb, &gb_rom_read, &gb_cart_ram_read,
                      &gb_cart_ram_write, &gb_error, NULL);
        DBG_INFO("GB ");

        if (ret != GB_INIT_NO_ERROR)
        {
            DBG_INFO("Error: %d\n", ret);
            goto out;
        }

#if ENABLE_SDCARD
        /* Try to load last saved emulator state for this game. */
        read_gb_emulator_state(&gb);
#endif

        /* Automatically assign a colour palette to the game */
        char rom_title[16];
        auto_assign_palette(palette, gb_colour_hash(&gb), gb_get_rom_name(&gb, rom_title));

#if ENABLE_LCD
        gb_init_lcd(&gb, &lcd_draw_line);
        DBG_INFO("LCD ");
#endif

#if ENABLE_SDCARD
        /* Load Save File. */
        read_cart_ram_file(&gb);
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
                multicore_fifo_push_blocking_inline(AUDIO_CMD_PLAYBACK);
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
            gb.direct.joypad_bits.up = input_pins[KEY_UP]==0?1:0;
            gb.direct.joypad_bits.down = input_pins[KEY_DOWN]==0?1:0;
            gb.direct.joypad_bits.left = input_pins[KEY_LEFT]==0?1:0;
            gb.direct.joypad_bits.right = input_pins[KEY_RIGHT]==0?1:0;
            gb.direct.joypad_bits.a = input_pins[KEY_A]==0?1:0;
            gb.direct.joypad_bits.b = input_pins[KEY_B]==0?1:0;
            gb.direct.joypad_bits.select = input_pins[KEY_SELECT]==0?1:0;
            gb.direct.joypad_bits.start = input_pins[KEY_START]==0?1:0;

            /* hotkeys (select + * combo)*/
            if (!gb.direct.joypad_bits.select)
            {
#if ENABLE_SOUND
                if (!gb.direct.joypad_bits.up && prev_joypad_bits.up)
                {
                    /* select + up: increase sound volume */
                    multicore_fifo_push_blocking_inline(AUDIO_CMD_VOLUME_UP);
                }
                if (!gb.direct.joypad_bits.down && prev_joypad_bits.down)
                {
                    /* select + down: decrease sound volume */
                    multicore_fifo_push_blocking_inline(AUDIO_CMD_VOLUME_DOWN);
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
#if 0
        static bool invert = false;
        static bool sleep = false;
        static uint8_t freq = 1;
        static ili9225_color_mode_e colour = ILI9225_COLOR_MODE_FULL;

        case 'i':
            invert = !invert;
            ili9225_display_control(invert, colour);
            break;

        case 'f':
            freq++;
            freq &= 0x0F;
            ili9225_set_drive_freq(freq);
            DBG_INFO("Freq %u\n", freq);
            break;
#endif
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
