#include "key.h"

// 内部状态枚举
typedef enum {
    ST_IDLE = 0,
    ST_DEBOUNCE,
    ST_PRESS,
    ST_WAIT
} KeyState_t;

static KeyState_t s_state = ST_IDLE;
static uint16_t s_timer = 0;                  // 状态内计时器
static KeyEvent_t s_event = KEY_EVENT_NONE;   // 存放待取走的事件

// 底层读取按键引脚（按键接在 PA7，上拉输入，按下为 0）
static uint8_t Key_ReadPin(void) {
    return (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_7) == GPIO_PIN_RESET);
}

void Key_Init(void) {
    s_state = ST_IDLE;
    s_timer = 0;
    s_event = KEY_EVENT_NONE;
}

// 核心状态机扫描函数（假设每 1ms 调用一次）
void Key_Scan(void) {
    uint8_t is_down = Key_ReadPin();

    switch (s_state) {
        case ST_IDLE:
            if (is_down) {
                s_state = ST_DEBOUNCE;
                s_timer = 0;
            }
            break;

        case ST_DEBOUNCE:
            if (!is_down) {
                s_state = ST_IDLE; // 没挺过 20ms，判定为杂波，退回
            } else if (++s_timer >= 20) { // 持续低电平满 20ms，消抖成功！
                s_state = ST_PRESS;
                s_timer = 0;
            }
            break;

        case ST_PRESS:
            if (!is_down) {
                // 在 800ms 内松开了，进入等待双击状态
                s_state = ST_WAIT;
                s_timer = 0;
            } else if (++s_timer >= 800) {
                // 按住超过 800ms，判定为长按！
                s_event = KEY_EVENT_LONG;
                s_state = ST_IDLE; // 处理完长按，重置待机
            }
            break;

        case ST_WAIT:
            if (is_down) {
                // 在 250ms 窗口期内又按了第二下！判定为双击！
                s_event = KEY_EVENT_DOUBLE;
                s_state = ST_IDLE;
            } else if (++s_timer >= 250) {
                // 等了 250ms 没动静，确认为单击！
                s_event = KEY_EVENT_SHORT;
                s_state = ST_IDLE;
            }
            break;
    }
}

// 读后清零：取走事件后，盒子置空
KeyEvent_t Key_GetEvent(void) {
    KeyEvent_t e = s_event;
    s_event = KEY_EVENT_NONE;
    return e;
}