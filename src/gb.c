#include "gb.h"
#include "shared.h"
#include "config.h"

#ifndef PEANUT_GB_H
#include "peanut_gb.h"
#endif

#include BUFFER_INCLUDE

uint8_t gb_rom_read(struct gb_s *gb, const uint_fast32_t addr)
{
    (void)gb;
    if (addr < BUFFER_ROM_BANK0_SIZE)
    {
        uint8_t val;
        BUFFER_ROM_BANK0_READ(addr, &val, 1);
        return val;
    }
    uint8_t val;
    BUFFER_ROM_BUFFER_READ(addr, &val, 1);
    return val;
}

uint8_t gb_cart_ram_read(struct gb_s *gb, const uint_fast32_t addr)
{
    (void)gb;
    uint8_t val;
    BUFFER_RAM_BUFFER_READ(addr, &val, 1);
    return val;
}

void gb_cart_ram_write(struct gb_s *gb, const uint_fast32_t addr,
                       const uint8_t val)
{
    BUFFER_RAM_BUFFER_WRITE(addr, &val, 1);
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