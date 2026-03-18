#include "state.h"
#include "shared.h"

#include SD_INCLUDE

#define PEANUT_GB_HEADER_ONLY
#ifndef PEANUT_GB_H
#include "peanut_gb.h"
#endif

void read_gb_emulator_state(struct gb_s *gb)
{
    char filename[16];
    char filename_state[32];
    gb_get_rom_name(gb, filename);
    sprintf(filename_state, "%s_state.bin", filename);
    SD_FILE_READ(filename_state, (uint8_t *)gb, sizeof(struct gb_s));
}

void write_gb_emulator_state(struct gb_s *gb)
{
    char filename[16];
    char filename_state[32];
    gb_get_rom_name(gb, filename);
    sprintf(filename_state, "%s_state.bin", filename);
    SD_FILE_WRITE(filename_state, (uint8_t *)gb, sizeof(struct gb_s));
}