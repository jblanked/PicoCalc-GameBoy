#include "gb.h"
#include "shared.h"

#ifndef PEANUT_GB_H
#include "peanut_gb.h"
#endif

uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
    (void)gb;
    if (addr < sizeof(rom_bank0))
        return rom_bank0[addr];

    return rom[addr];
}

uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
    (void)gb;
    return ram[addr];
}

void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr,
                       const uint8_t val)
{
    ram[addr] = val;
}

void gb_error(struct gb_s *gb, const enum gb_error_e gb_err, const uint16_t addr)
{
    const char *gb_err_str[4] = {
        "UNKNOWN",
        "INVALID OPCODE",
        "INVALID READ",
        "INVALID WRITE"};
    DBG_INFO("Error %d occurred: %s at %04X\n.\n", gb_err, gb_err_str[gb_err], addr);
}