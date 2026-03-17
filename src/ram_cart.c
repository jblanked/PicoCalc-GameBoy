#include "ram_cart.h"
#include "shared.h"
#include "sdcard.h"

#define PEANUT_GB_HEADER_ONLY
#ifndef PEANUT_GB_H
#include "peanut_gb.h"
#endif

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