add_library(ili9488_p STATIC
    picocalc.c
    mono8x16.c
    mono8x16_bmp.c
    i2ckbd.c
)

pico_generate_pio_header(ili9488_p ${CMAKE_CURRENT_LIST_DIR}/picocalc.pio)

target_include_directories(ili9488_p PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ili9488_p
    pico_stdlib
    hardware_spi
    hardware_pio
    hardware_i2c
    hardware_dma
    hardware_gpio
    hardware_pwm
    hardware_irq
    pico_multicore
)
