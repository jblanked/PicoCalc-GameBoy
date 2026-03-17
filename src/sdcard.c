/**
 * sdcard.c
 *
 * SD card hardware configuration and driver callbacks required by the
 * no-OS-FatFS-SD-SPI-RPi-Pico library.  All definitions live here so that
 * the functions have a single definition at link time.
 */

#include "sdcard.h"

static spi_t spis[] = {
    {
        .hw_inst = spi0,
        .miso_gpio = 16,
        .mosi_gpio = 19,
        .sck_gpio = 18,
        .baud_rate = 125000000 / 2 / 4,
        .dma_isr = spi_dma_isr
    }
};

static sd_card_t sd_cards[] = {
    {
        .pcName = "0:",
        .spi = &spis[0],
        .ss_gpio = 17,
        .use_card_detect = false,
        .m_Status = STA_NOINIT
    }
};

void spi_dma_isr(void) {
    spi_irq_handler(&spis[0]);
}

size_t sd_get_num(void) {
    return count_of(sd_cards);
}

sd_card_t *sd_get_by_num(size_t num) {
    if (num <= sd_get_num()) {
        return &sd_cards[num];
    } else {
        return NULL;
    }
}

size_t spi_get_num(void) {
    return count_of(spis);
}

spi_t *spi_get_by_num(size_t num) {
    if (num <= sd_get_num()) {
        return &spis[num];
    } else {
        return NULL;
    }
}
