#include "q4_app.h"
#include "oled.h"
#include "w25q64.h"
#include "bsp.h"
#include "adc.h"
#include <stdio.h>
#include <stdlib.h>

typedef enum {
    ST_LOGIN = 0,
    ST_DENIED,
    ST_MENU,
    ST_MONITOR,
    ST_SERVO,
    ST_OLD_PWD,
    ST_NEW_PWD,
    ST_NEW_AGAIN,
    ST_CHANGE_OK
} AppState_t;

static AppState_t s_app_state = ST_LOGIN;

static uint8_t s_sys_pwd[4]   = {1, 2, 3, 4}; 
static uint8_t s_input_pwd[4] = {0, 0, 0, 0}; 
static uint8_t s_new_pwd[4]   = {0, 0, 0, 0}; 
static uint8_t s_cursor       = 0;            
static uint8_t s_is_editing   = 0;            

static uint8_t s_menu_idx     = 0;            
static int16_t s_servo_angle  = 90;           

extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

// ---------------- 1. 蜂鸣器严格适配低电平触发 (PB3) ----------------
// 低电平 0 响，高电平 1 关
#define BUZZER_ON()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET)
#define BUZZER_OFF()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET)

static void Beep_Short(void) {
    BUZZER_ON();
    HAL_Delay(80);
    BUZZER_OFF();
}

static void Beep_Long(void) {
    BUZZER_ON();
    HAL_Delay(600);
    BUZZER_OFF();
}

// ---------------- 2. 毫秒级极速响应按键检测 (PB6) ----------------
static uint8_t s_sw_event = 0;

void Encoder_Key_Scan(void)
{
    static uint8_t last_pin_state = 1; // 上拉输入，默认高电平 1
    static uint32_t last_press_time = 0;

    uint8_t cur_pin_state = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6);

    // 捕捉下降沿：从 1 变 0 的瞬间判定为刚按下去！
    if (last_pin_state == 1 && cur_pin_state == 0) {
        // 时间窗防抖：距离上一次有效触发大于 120ms，彻底杜绝机械回弹
        if (HAL_GetTick() - last_press_time > 120) {
            s_sw_event = 1; // 发货！
            last_press_time = HAL_GetTick();
        }
    }
    last_pin_state = cur_pin_state;
}

uint8_t Encoder_Key_Pressed(void)
{
    uint8_t event = s_sw_event;
    s_sw_event = 0; // 读后清零
    return event;
}

// ---------------- 3. 电机调速 (TIM1_CH1 / PB12 / PB13) ----------------
void Motor_SetSpeed(int speed)
{
    uint32_t current_speed = (uint32_t)(3599 * (abs(speed) / 100.0f));

    if (speed > 0) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, current_speed);
    } else if (speed < 0) {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, current_speed);
    } else {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    }
}

// ---------------- 4. 编码器旋转步数提取 ----------------
static int16_t Get_Encoder_Step(void) {
    static int16_t last_cnt = 0;
    int16_t cur_cnt = (int16_t)__HAL_TIM_GET_COUNTER(&htim3);
    int16_t delta = cur_cnt - last_cnt;
    
    // 机械旋钮一格4个跳变，每满2个或4个计算一步
    if (abs(delta) >= 2) {
        int16_t step = delta / 2;
        last_cnt += step * 2;
        if (abs(step) >= 2) {
            return step / 2;
        }
    }
    return 0;
}

static void Servo_Update(int16_t angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    uint32_t pulse = 500 + (angle * 2000) / 180;
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, pulse);
}

// ---------------- 5. 密码输入界面交互 ----------------
static uint8_t Password_Input_Handler(uint8_t *pwd_buf, const char *title)
{
    int16_t step = Get_Encoder_Step();
    uint8_t clicked = Encoder_Key_Pressed();

    // 旋转操作
    if (step != 0) {
        if (s_is_editing) {
            // 编辑数字：0~9
            int16_t val = (int16_t)pwd_buf[s_cursor] + step;
            while (val < 0) val += 10;
            while (val > 9) val -= 10;
            pwd_buf[s_cursor] = (uint8_t)val;
        } else {
            // 移动光标：0~4 (4 对应 'e')
            int16_t cur = (int16_t)s_cursor + step;
            while (cur < 0) cur += 5;
            while (cur > 4) cur -= 5;
            s_cursor = (uint8_t)cur;
        }
    }

    // 按下操作：瞬间响应！
    if (clicked) {
        if (s_cursor == 4) {
            return 1; // 在 'e' 上提交验证！
        } else {
            s_is_editing = !s_is_editing; // 切换编辑/移动状态
        }
    }

    // 状态提示行
    if (s_is_editing) {
        OLED_ShowString(0, 0, "EDITING VALUE...");
    } else {
        OLED_ShowString(0, 0, (char *)title);
    }

    // 密码行显示：4个数字 + 间距 + 'e'
    char pwd_line[20];
    uint8_t blink = (HAL_GetTick() / 300) % 2;

    for (int i = 0; i < 4; i++) {
        if (s_is_editing && s_cursor == i && blink == 0) {
            pwd_line[i * 3] = ' '; // 闪烁！
        } else {
            pwd_line[i * 3] = '0' + pwd_buf[i];
        }
        pwd_line[i * 3 + 1] = ' ';
        pwd_line[i * 3 + 2] = ' ';
    }
    pwd_line[12] = 'e'; // 正立标致的小写 e！
    pwd_line[13] = '\0';
    OLED_ShowString(16, 2, pwd_line);

    // 光标指示行：清晰可见的 '^'
    char cursor_line[20] = "                    ";
    cursor_line[s_cursor * 3] = '^'; // 纯正光标箭头！
    cursor_line[14] = '\0';
    OLED_ShowString(16, 3, cursor_line);

    return 0;
}

// ---------------- 6. 全局初始化 ----------------
void Q4_Init(void) {
    BUZZER_OFF(); // 👈 开机第一行代码：死死拉高 PB3，彻底静音！
    W25Q64_Init();
    W25Q64_LoadPassword(s_sys_pwd); 
    Servo_Update(s_servo_angle);     
    s_app_state = ST_LOGIN;
}

// ---------------- 7. 主任务调度 ----------------
void Q4_Task(void)
{
    // 按键高频扫描：无延迟精准捕获下压瞬间
    Encoder_Key_Scan();

    switch (s_app_state)
    {
        case ST_LOGIN:
            if (Password_Input_Handler(s_input_pwd, "PASSWORD LOGIN  ")) {
                if (s_input_pwd[0] == s_sys_pwd[0] && s_input_pwd[1] == s_sys_pwd[1] &&
                    s_input_pwd[2] == s_sys_pwd[2] && s_input_pwd[3] == s_sys_pwd[3]) 
                {
                    Beep_Short();
                    OLED_Clear();
                    s_app_state = ST_MENU;
                } else {
                    Beep_Long();
                    OLED_Clear();
                    s_app_state = ST_DENIED;
                }
            }
            break;

        case ST_DENIED:
            OLED_ShowString(8, 1, "PASSWORD ERROR ");
            OLED_ShowString(16, 2, "PLEASE RETRY...");
            HAL_Delay(1200);
            s_input_pwd[0] = s_input_pwd[1] = s_input_pwd[2] = s_input_pwd[3] = 0;
            s_cursor = 0; s_is_editing = 0;
            OLED_Clear();
            s_app_state = ST_LOGIN;
            break;

        case ST_MENU: {
            int16_t step = Get_Encoder_Step();
            if (step != 0) {
                int16_t m = (int16_t)s_menu_idx + step;
                while (m < 0) m += 3;
                while (m > 2) m -= 3;
                s_menu_idx = (uint8_t)m;
            }

            OLED_ShowString(0, 0, "=== MAIN MENU ===");
            OLED_ShowString(8, 1, (s_menu_idx == 0) ? "> 1. MONITOR " : "  1. MONITOR ");
            OLED_ShowString(8, 2, (s_menu_idx == 1) ? "> 2. SERVO   " : "  2. SERVO   ");
            OLED_ShowString(8, 3, (s_menu_idx == 2) ? "> 3. CHG-PWD " : "  3. CHG-PWD ");

            if (Encoder_Key_Pressed()) {
                OLED_Clear();
                if (s_menu_idx == 0) s_app_state = ST_MONITOR;
                if (s_menu_idx == 1) s_app_state = ST_SERVO;
                if (s_menu_idx == 2) {
                    s_input_pwd[0] = s_input_pwd[1] = s_input_pwd[2] = s_input_pwd[3] = 0;
                    s_cursor = 0; s_is_editing = 0;
                    s_app_state = ST_OLD_PWD;
                }
            }
            break;
        }

        case ST_MONITOR: {
            if (Encoder_Key_Pressed()) {
                Motor_SetSpeed(0);
                BSP_LedAll(0x00);
                BUZZER_OFF();
                OLED_Clear();
                s_app_state = ST_MENU;
                break;
            }

            uint16_t light = Adc_GetLight();
            int16_t temp10 = Adc_GetTemperature10();

            int motor_speed = 0;
            if (temp10 < 300) {
                motor_speed = 0;
            } else if (temp10 < 350) {
                motor_speed = 40;
            } else if (temp10 < 400) {
                motor_speed = 70;
            } else {
                motor_speed = 100;
            }
            Motor_SetSpeed(motor_speed);

            if (temp10 >= 450) {
                uint8_t alarm_blink = (HAL_GetTick() / 250) % 2;
                BSP_LedAll(alarm_blink ? 0xFF : 0x00);
                if (alarm_blink) { BUZZER_ON(); } else { BUZZER_OFF(); }
            } else {
                BSP_LedAll(0x00);
                BUZZER_OFF();
            }

            char buf[32];
            OLED_ShowString(0, 0, "MONITOR [K:EXIT]");
            sprintf(buf, "L:%04d  T:%2d.%dC", light, temp10 / 10, abs(temp10 % 10));
            OLED_ShowString(0, 1, buf);
            sprintf(buf, "FAN: %3d %%", motor_speed);
            OLED_ShowString(0, 2, buf);
            sprintf(buf, "STATE: %s", (motor_speed > 0) ? "RUN " : "STOP");
            OLED_ShowString(0, 3, buf);
            break;
        }

        case ST_SERVO: {
            if (Encoder_Key_Pressed()) {
                OLED_Clear();
                s_app_state = ST_MENU;
                break;
            }

            int16_t step = Get_Encoder_Step();
            if (step != 0) {
                s_servo_angle += step * 5;
                if (s_servo_angle < 0) s_servo_angle = 0;
                if (s_servo_angle > 180) s_servo_angle = 180;
                Servo_Update(s_servo_angle);
            }

            OLED_ShowString(0, 0, "SERVO [K:EXIT]");
            char s_buf[32];
            sprintf(s_buf, "ANGLE: %3d DEG ", s_servo_angle);
            OLED_ShowString(8, 2, s_buf);
            break;
        }

        case ST_OLD_PWD:
            if (Password_Input_Handler(s_input_pwd, "VERIFY OLD PWD  ")) {
                if (s_input_pwd[0] == s_sys_pwd[0] && s_input_pwd[1] == s_sys_pwd[1] &&
                    s_input_pwd[2] == s_sys_pwd[2] && s_input_pwd[3] == s_sys_pwd[3]) 
                {
                    Beep_Short();
                    s_new_pwd[0] = s_new_pwd[1] = s_new_pwd[2] = s_new_pwd[3] = 0;
                    s_cursor = 0; s_is_editing = 0;
                    OLED_Clear();
                    s_app_state = ST_NEW_PWD;
                } else {
                    Beep_Long();
                    OLED_Clear();
                    s_app_state = ST_DENIED;
                }
            }
            break;

        case ST_NEW_PWD:
            if (Password_Input_Handler(s_new_pwd, "ENTER NEW PWD   ")) {
                s_input_pwd[0] = s_input_pwd[1] = s_input_pwd[2] = s_input_pwd[3] = 0;
                s_cursor = 0; s_is_editing = 0;
                OLED_Clear();
                s_app_state = ST_NEW_AGAIN;
            }
            break;

        case ST_NEW_AGAIN:
            if (Password_Input_Handler(s_input_pwd, "CONFIRM PWD     ")) {
                if (s_input_pwd[0] == s_new_pwd[0] && s_input_pwd[1] == s_new_pwd[1] &&
                    s_input_pwd[2] == s_new_pwd[2] && s_input_pwd[3] == s_new_pwd[3]) 
                {
                    for (int i = 0; i < 4; i++) s_sys_pwd[i] = s_new_pwd[i];
                    W25Q64_SavePassword(s_sys_pwd);
                    Beep_Short();
                    OLED_Clear();
                    s_app_state = ST_CHANGE_OK;
                } else {
                    Beep_Long();
                    OLED_Clear();
                    s_app_state = ST_DENIED;
                }
            }
            break;

        case ST_CHANGE_OK:
            OLED_ShowString(16, 1, "PWD SAVED OK!");
            OLED_ShowString(8, 2, "FLASH UPDATED");
            HAL_Delay(1500);
            OLED_Clear();
            s_app_state = ST_MENU;
            break;
    }
}