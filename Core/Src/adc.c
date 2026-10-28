#include "adc.h"
#include <math.h>

extern ADC_HandleTypeDef hadc1;

// 底层通道动态切换读取函数
static uint16_t Adc_ReadChannel(uint32_t channel)
{
    ADC_ChannelConfTypeDef cfg = {0};
    uint16_t value = 0;

    cfg.Channel = channel;
    cfg.Rank = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_55CYCLES_5; // 55.5 周期采样，阻抗匹配极稳

    if (HAL_ADC_ConfigChannel(&hadc1, &cfg) != HAL_OK) return 0;
    if (HAL_ADC_Start(&hadc1) != HAL_OK) return 0;
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
    {
        value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    }
    HAL_ADC_Stop(&hadc1);
    return value;
}

void Adc_Init(void)
{
    // ADC1 已经在 main.c 里完成了硬件初始化，这里预留
}

// 1. 读取光敏数据 (PB0 对应 ADC1 通道 8)
uint16_t Adc_GetLight(void)
{
    return Adc_ReadChannel(ADC_CHANNEL_8);
}

// 2. 读取 NTC 温度并计算摄氏度 (PB1 对应 ADC1 通道 9)
float Adc_GetTemperature(void)
{
    float adc = (float)Adc_ReadChannel(ADC_CHANNEL_9);
    
    // 边界保护：防止分母为 0 或对数算负数导致死机
    if (adc < 1.0f) adc = 1.0f;
    if (adc > 4094.0f) adc = 4094.0f;

    // ① 根据分压公式反推 NTC 当前阻值
    float r_ntc = NTC_R_SERIES * adc / (4095.0f - adc);

    // ② B 值方程逆运算解开尔文温度 (T = 1 / (1/T0 + ln(R/R0) / B))
    const float T0 = 273.15f + 25.0f; // 25℃ 对应的开尔文温度
    float temp_k = 1.0f / ((1.0f / T0) + (logf(r_ntc / NTC_R25) / NTC_B_VALUE));

    // ③ 开尔文转摄氏度
    return temp_k - 273.15f;
}

// 返回放大 10 倍的整数值 (例如 25.6℃ 返回 256)
int16_t Adc_GetTemperature10(void)
{
    float t = Adc_GetTemperature();
    if (t < 0.0f) t = 0.0f;
    if (t > 99.9f) t = 99.9f;
    return (int16_t)(t * 10.0f + 0.5f); // 四舍五入
}