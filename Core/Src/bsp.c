#include "bsp.h"

// 1. 结构体扩展：每个引脚不仅记录端口和针脚，还记录“用什么电平点亮”
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState on_level; // GPIO_PIN_RESET(低电平亮) 或 GPIO_PIN_SET(高电平亮)
} LedPin_t;

// 2. 引脚配置表：在这里为每个灯量身定制物理特性
static const LedPin_t s_leds[8] = {
    {GPIOC, GPIO_PIN_13, GPIO_PIN_RESET}, // LED1 (板载开漏灌电流)：输出 0 亮
    {GPIOA, GPIO_PIN_0,  GPIO_PIN_SET},   // LED2 (外接推挽到地)：输出 1 亮
    {GPIOA, GPIO_PIN_1,  GPIO_PIN_SET},   // LED3 (外接推挽到地)：输出 1 亮
    {GPIOA, GPIO_PIN_2,  GPIO_PIN_SET},   // LED4
    {GPIOA, GPIO_PIN_3,  GPIO_PIN_SET},   // LED5
    {GPIOA, GPIO_PIN_4,  GPIO_PIN_SET},   // LED6
    {GPIOA, GPIO_PIN_5,  GPIO_PIN_SET},   // LED7
    {GPIOA, GPIO_PIN_6,  GPIO_PIN_SET}    // LED8
};

// 单灯控制底层抽象函数
void BSP_LedWrite(uint8_t idx, uint8_t on) {
    if (idx >= 8) return;

    GPIO_PinState pin_state;
    if (on) {
        // 如果要开灯，直接使用该引脚预设的“点亮电平”
        pin_state = s_leds[idx].on_level;
    } else {
        // 如果要关灯，取反即可
        pin_state = (s_leds[idx].on_level == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }

    HAL_GPIO_WritePin(s_leds[idx].port, s_leds[idx].pin, pin_state);
}

// 初始化：上电默认 8 个灯全灭
void BSP_Init(void) {
    BSP_LedAll(0x00);
}

// 核心函数：8 路 LED 统一控制
void BSP_LedAll(uint8_t mask) {
    for (uint8_t i = 0; i < 8; i++) {
        // 取出第 i 位：1 代表开灯，0 代表关灯
        uint8_t is_on = (mask >> i) & 0x01U;
        BSP_LedWrite(i, is_on);
    }
}