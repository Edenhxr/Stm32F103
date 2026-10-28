#ifndef __ADC_H
#define __ADC_H

#include "main.h"

// 传感器参数定义
#define NTC_B_VALUE      3950.0f    // NTC 的 B 常数
#define NTC_R25          10000.0f   // 25℃ 时的标称阻值 10k
#define NTC_R_SERIES     10000.0f   // 串联的分压电阻 10k

void Adc_Init(void);
uint16_t Adc_GetLight(void);        // 获取光照原始 ADC 值 (0 ~ 4095)
float Adc_GetTemperature(void);     // 获取当前摄氏度浮点数 (如 25.6℃)
int16_t Adc_GetTemperature10(void); // 获取放大10倍的整数 (25.6℃ 返回 256，省开销)

#endif