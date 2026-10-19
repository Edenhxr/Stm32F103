#include "bsp.h"

// 定义一个结构体把引脚打包
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} LedPin_t;

// 8个 LED 的物理引脚查找表（存入 Flash，省 RAM）
static const LedPin_t s_leds[8] = {
    {GPIOC, GPIO_PIN_13}, // LED1 (板载)
    {GPIOA, GPIO_PIN_0},  // LED2
    {GPIOA, GPIO_PIN_1},  // LED3
    {GPIOA, GPIO_PIN_2},  // LED4
    {GPIOA, GPIO_PIN_3},  // LED5
    {GPIOA, GPIO_PIN_4},  // LED6
    {GPIOA, GPIO_PIN_5},  // LED7
    {GPIOA, GPIO_PIN_6}   // LED8
};

// 初始化：上电默认让 8 个灯全灭
void BSP_Init(void) {
    BSP_LedAll(0x00);
}

// 核心函数：位运算控制 8 路 LED
void BSP_LedAll(uint8_t mask) {
    for (uint8_t i = 0; i < 8; i++) {
        // 取出第 i 位：是 1 还是 0
        uint8_t is_on = (mask >> i) & 0x01U;
        
        // 低电平点亮：如果是 1，输出 RESET(低电平)；如果是 0，输出 SET(高电平关灯)
        HAL_GPIO_WritePin(s_leds[i].port, s_leds[i].pin, 
                          is_on ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }
}