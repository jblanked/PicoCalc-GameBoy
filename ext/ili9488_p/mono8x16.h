#include "stdint.h"

extern const uint8_t bmp[];

void draw_char_rgb444(
    uint8_t *dest,
    int dest_stride,
    int dest_w,
    int dest_h,
    int x0,
    int y0,
    const char c,
    uint16_t color
);

void draw_string_rgb444(
    uint8_t *dest,
    int dest_stride,
    int dest_w,
    int dest_h,
    int x0,
    int y0,
    const char* str,
    uint16_t color
);

void draw_char_rgb565(
    uint8_t *dest,
            int dest_stride,
            int dest_w,
            int dest_h,
            int x0,
            int y0,
            const char c,
            uint16_t color
    );

    void draw_string_rgb565(
            uint8_t *dest,
            int dest_stride,
            int dest_w,
            int dest_h,
            int x0,
            int y0,
            const char* str,
            uint16_t color
);
