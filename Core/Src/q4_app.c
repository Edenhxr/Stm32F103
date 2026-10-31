#include "q4_app.h"
#include "oled.h"
#include "w25q64.h"
#include "bsp.h"
#include "adc.h"
#include <stdio.h>
#include <stdlib.h>

// 状态机枚举定义
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

// 密码相关变量 (4 位数字)
static uint8_t s_sys_pwd[4]   = {1, 2, 3, 4}; // 系统当前密码 (开机从 W25Q64 读出)
static uint8_t s_input_pwd[4] = {0, 0, 0, 0}; // 用户正在输入的密码
static uint8_t s_new_pwd[4]   = {0, 0, 0, 0}; // 修改密码时的新密码
static uint8_t s_cursor       = 0;            // 光标位置: 0~3 对应第1~4位数字，4 对应 'e'
static uint8_t s_is_editing   = 0;            // 0: 移动光标状态, 1: 编辑该位数字状态

// 菜单与舵机变量
static uint8_t s_menu_idx     = 0;            // 0: 光热监控, 1: 舵机控制, 2: 密码修改
static int16_t s_servo_angle  = 90;           // 舵机初始角度 90度

// 外部硬件定时器句柄声明
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;

// ---------------- 1. 编码器按压按键底层驱动 (PB6) ----------------
static uint8_t s_sw_cnt = 0;
static uint8_t s_sw_stable = 0;
static uint8_t s_sw_event = 0;

void Encoder_Key_Scan(void)
{
    uint8_t is_down = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_RESET);
    if (is_down) {
        if (s_sw_cnt < 15) {
            s_sw_cnt++;
            if (s_sw_cnt >= 15 && s_sw_stable == 0) {
                s_sw_stable = 1;
                s_sw_event = 1; // 产生按下事件
            }
        }
    } else {
        s_sw_cnt = 0;
        s_sw_stable = 0;
    }
}

uint8_t Encoder_Key_Pressed(void)
{
    uint8_t event = s_sw_event;
    s_sw_event = 0; // 读后清零
    return event;
}

// ---------------- 2. 电机调速底层驱动 (TIM1_CH1 / PB12 / PB13) ----------------
void Motor_SetSpeed(int speed)
{
    uint32_t current_speed = (uint32_t)(3599 * (abs(speed) / 100.0f));

    if (speed > 0) // 正转
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, current_speed);
    } 
    else if (speed < 0) // 反转
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, current_speed);
    } 
    else // 停转
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    }
}

// ---------------- 3. 编码器旋转步数提取 ----------------
static int16_t Get_Encoder_Step(void) {
    static int16_t last_cnt = 0;
    int16_t cur_cnt = (int16_t)__HAL_TIM_GET_COUNTER(&htim3);
    int16_t delta = cur_cnt - last_cnt;
    if (abs(delta) >= 4) {
        int16_t step = delta / 4;
        last_cnt += step * 4;
        return step;
    }
    return 0;
}

// ---------------- 4. 蜂鸣器提示音 ----------------
static void Beep_Short(void) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET); // 响
    HAL_Delay(80);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);   // 关
}

static void Beep_Long(void) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
    HAL_Delay(600);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
}

// ---------------- 5. 舵机角度驱动 (PB7) ----------------
static void Servo_Update(int16_t angle) {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    uint32_t pulse = 500 + (angle * 2000) / 180;
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, pulse);
}

// ---------------- 6. 密码输入界面通用逻辑 ----------------
static uint8_t Password_Input_Handler(uint8_t *pwd_buf, const char *title)
{
    int16_t step = Get_Encoder_Step();
    uint8_t clicked = Encoder_Key_Pressed();

    if (step != 0) {
        if (s_is_editing) {
            int16_t val = (int16_t)pwd_buf[s_cursor] + step;
            while (val < 0) val += 10;
            while (val > 9) val -= 10;
            pwd_buf[s_cursor] = (uint8_t)val;
        } else {
            int16_t cur = (int16_t)s_cursor + step;
            while (cur < 0) cur += 5;
            while (cur > 4) cur -= 5;
            s_cursor = (uint8_t)cur;
        }
    }

    if (clicked) {
        if (s_cursor == 4) {
            return 1; // 确认提交
        } else {
            s_is_editing = !s_is_editing; // 切换编辑模式
        }
    }

    OLED_ShowString(0, 0, (char *)title);

    char pwd_line[16];
    uint8_t blink = (HAL_GetTick() / 300) % 2;

    for (int i = 0; i < 4; i++) {
        if (s_is_editing && s_cursor == i && blink == 0) {
            pwd_line[i * 2] = ' ';
        } else {
            pwd_line[i * 2] = '0' + pwd_buf[i];
        }
        pwd_line[i * 2 + 1] = ' ';
    }
    pwd_line[8] = 'e';
    pwd_line[9] = '\0';
    OLED_ShowString(16, 2, pwd_line);

    char cursor_line[16] = "                ";
    cursor_line[s_cursor * 2] = '^';
    cursor_line[9] = '\0';
    OLED_ShowString(16, 3, cursor_line);

    return 0;
}

// ---------------- 7. 第 4 题全局初始化 ----------------
void Q4_Init(void) {
    W25Q64_Init();
    W25Q64_LoadPassword(s_sys_pwd); // 从 Flash 加载密码 (默认 1234)
    Servo_Update(s_servo_angle);     // 舵机初始 90°
    s_app_state = ST_LOGIN;
}

// ---------------- 8. 第 4 题主任务调度 ----------------
void Q4_Task(void)
{
    static uint32_t last_scan = 0;
    if (HAL_GetTick() - last_scan >= 1) {
        last_scan = HAL_GetTick();
        Encoder_Key_Scan();
    }

    switch (s_app_state)
    {
        case ST_LOGIN:
            if (Password_Input_Handler(s_input_pwd, "PASSWORD LOGIN")) {
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
            OLED_ShowString(16, 1, "PASSWORD ERROR");
            OLED_ShowString(24, 2, "TRY AGAIN...");
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
                if (m < 0) m = 2;
                if (m > 2) m = 0;
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
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
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
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, alarm_blink ? GPIO_PIN_RESET : GPIO_PIN_SET);
            } else {
                BSP_LedAll(0x00);
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
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
            if (Password_Input_Handler(s_input_pwd, "VERIFY OLD PWD")) {
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
            if (Password_Input_Handler(s_new_pwd, "ENTER NEW PWD")) {
                s_input_pwd[0] = s_input_pwd[1] = s_input_pwd[2] = s_input_pwd[3] = 0;
                s_cursor = 0; s_is_editing = 0;
                OLED_Clear();
                s_app_state = ST_NEW_AGAIN;
            }
            break;

        case ST_NEW_AGAIN:
            if (Password_Input_Handler(s_input_pwd, "CONFIRM PWD")) {
                if (s_input_pwd[0] == s_new_pwd[0] && s_input_pwd[1] == s_new_pwd[1] &&
                    s_input_pwd[2] == s_new_pwd[2] && s_input_pwd[3] == s_new_pwd[3]) 
                {
                    for (int i = 0; i < 4; i++) s_sys_pwd[i] = s_new_pwd[i];
                    W25Q64_SavePassword(s_sys_pwd); // 写入 Flash 持久化保存
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