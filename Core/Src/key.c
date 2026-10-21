#include "key.h"

// 内部私有状态定义（增加了一个 ST_WAIT_RELEASE）
typedef enum {
    ST_IDLE = 0,        // 空闲
    ST_DEBOUNCE,        // 20ms 按下消抖
    ST_PRESS,           // 按住计时
    ST_WAIT,            // 等待双击
    ST_WAIT_RELEASE     // 👈 新增：长按/双击触发后，等待用户彻底松手
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

// 核心状态机扫描函数（每 1ms 调用一次）
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
                s_state = ST_IDLE; // 杂波，退回
            } else if (++s_timer >= 20) { // 持续 20ms，确认为真按下
                s_state = ST_PRESS;
                s_timer = 0;
            }
            break;

        case ST_PRESS:
            if (!is_down) {
                // 800ms 内松手了，进入等待双击窗口
                s_state = ST_WAIT;
                s_timer = 0;
            } else if (++s_timer >= 800) {
                // 持续按住满 800ms，触发长按！
                s_event = KEY_EVENT_LONG;
                // 💡 修复点：绝不回 IDLE，转入等待松手状态！
                s_state = ST_WAIT_RELEASE; 
            }
            break;

        case ST_WAIT:
            if (is_down) {
                // 250ms 窗口内又按下了第二下，触发双击！
                s_event = KEY_EVENT_DOUBLE;
                // 💡 修复点：第二下按键还没松开，转入等待松手状态！
                s_state = ST_WAIT_RELEASE; 
            } else if (++s_timer >= 250) {
                // 250ms 过去没动静，确认为真单击！
                s_event = KEY_EVENT_SHORT;
                s_state = ST_IDLE;
            }
            break;

        // 💡 修复核心：安全吸收长按和双击的“松手动作”
        case ST_WAIT_RELEASE:
            if (!is_down) {
                // 只有当用户的手指彻底离开按键时，才平安回到待机状态
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