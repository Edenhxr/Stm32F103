#ifndef __W25Q64_H
#define __W25Q64_H

#include "main.h"

// 密码存储结构魔数（互补抗干扰模式）
#define PWD_MAGIC0     0xA5U
#define PWD_MAGIC1     0x5AU
#define PWD_ADDR       0x000000U // 密码保存在第 0 扇区开头

void W25Q64_Init(void);
uint32_t W25Q64_ReadID(void);                           // 读取芯片 ID（预期 0xEF4017）
void W25Q64_SavePassword(const uint8_t *pwd4);          // 保存新密码到 Flash
void W25Q64_LoadPassword(uint8_t *pwd4);                // 开机读取密码（带校验和默认值）

#endif