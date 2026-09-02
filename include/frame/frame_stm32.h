#ifndef FRAME_STM32_H
#define FRAME_STM32_H

#include <stdint.h>

typedef struct
{
    int16_t temp;
    uint16_t humi;
    uint16_t light;
} frame_stm32_t;

#endif