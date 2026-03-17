#include "ram_cart.h"
#include "shared.h"

#include SD_INCLUDE

#define PEANUT_GB_HEADER_ONLY
#ifndef PEANUT_GB_H
#include "peanut_gb.h"
#endif

void read_cart_ram_file(struct gb_s *gb)
{
    char filename[16];
    gb_get_rom_name(gb, filename);
    uint_fast32_t save_size = gb_get_save_size(gb);
    if (save_size > 0)
    {
        SD_FILE_READ(filename, ram, SD_FILE_SIZE(filename));
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
    gb_get_rom_name(gb, filename);
    uint_fast32_t save_size = gb_get_save_size(gb);
    if (save_size > 0)
    {
        SD_FILE_WRITE(filename, ram, save_size);
        DBG_INFO("I write_cart_ram_file(%s) COMPLETE (%lu bytes)\n", filename, bw);
    }
    else
    {
        DBG_INFO("I write_cart_ram_file(%s) SKIPPED\n", filename);
    }
}