#ifndef FRAME_SHT30_H
#define FRAME_SHT30_H

#include <stdint.h>

typedef struct
{
    uint16_t temp;
    uint16_t humi;
} frame_sht30_t;

#endif