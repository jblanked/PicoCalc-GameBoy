#include "state.h"
#include "sdcard.h"
#include "shared.h"

#define PEANUT_GB_HEADER_ONLY
#ifndef PEANUT_GB_H
#include "peanut_gb.h"
#endif

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