#ifndef __KEY_H
#define __KEY_H

#include "main.h"

// 状态机向外汇报的 4 种事件类型
typedef enum {
    KEY_EVENT_NONE = 0, // 无事件
    KEY_EVENT_SHORT,    // 单击
    KEY_EVENT_DOUBLE,   // 双击
    KEY_EVENT_LONG      // 长按
} KeyEvent_t;

void Key_Init(void);
void Key_Scan(void);               // 必须每 1ms 或 10ms 调用一次
KeyEvent_t Key_GetEvent(void);     // 读取当前事件（读完自动清零）

#endif