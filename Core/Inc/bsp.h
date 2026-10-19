#ifndef __BSP_H
#define __BSP_H

#include "main.h"

// 对外提供的极简接口
void BSP_Init(void);
void BSP_LedAll(uint8_t mask);      // 用 8 位二进制控制 8 个灯
void BSP_LedOn(uint8_t idx);        // 点亮第 idx 个灯 (0~7)
void BSP_LedOff(uint8_t idx);       // 熄灭第 idx 个灯 (0~7)

#endif