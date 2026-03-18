#ifndef __POCKETPICO_DEBUG__
#define __POCKETPICO_DEBUG__

#include <stdio.h>
#include <pico/stdio.h>

#if ENABLE_DEBUG
    #define DBG_INIT()     stdio_init_all()
    #define DBG_INFO(...)  printf(__VA_ARGS__)
#else
    #define DBG_INIT()
    #define DBG_INFO(...)
#endif

#endif