#ifndef __OLED_FONT_H
#define __OLED_FONT_H

#include "main.h"

#define OLED_FONT_CN_COUNT 15U

typedef struct
{
    uint16_t code;      /* Unicode 码点 */
    uint8_t data[32];   /* 16x16 点阵数据 (2页 x 16列) */
} OLED_CnGlyph_t;

extern const OLED_CnGlyph_t OLED_FontCn[OLED_FONT_CN_COUNT];

const uint8_t *OLED_FontCnLookup(uint16_t code);

#endif /* __OLED_FONT_H */
