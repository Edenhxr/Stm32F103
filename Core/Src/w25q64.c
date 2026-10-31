#include "w25q64.h"

// 软件 SPI 引脚控制宏
#define CS_L()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_RESET)
#define CS_H()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_12, GPIO_PIN_SET)
#define SCK_L()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET)
#define SCK_H()  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET)
#define MOSI_L() HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_RESET)
#define MOSI_H() HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_SET)
#define MISO_RD() (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_15) == GPIO_PIN_SET)

static void SPI_Delay(void) {
    for (volatile int i = 0; i < 30; i++); // 👈 稍微给杜邦线留几微秒充放电时间
}

// 核心：SPI 字节全双工交换 (模式 0: 空闲时钟低，上升沿采样)
static uint8_t SPI_SwapByte(uint8_t byte) {
    uint8_t rx = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (byte & 0x80) MOSI_H(); else MOSI_L();
        byte <<= 1;
        SPI_Delay();
        SCK_H(); // 上升沿：从机采样
        SPI_Delay();
        rx <<= 1;
        if (MISO_RD()) rx |= 0x01; // 主机同时采样
        SCK_L(); // 下降沿
        SPI_Delay();
    }
    return rx;
}

static void W25Q64_WriteEnable(void) {
    CS_L();
    SPI_SwapByte(0x06); // 写入使能指令
    CS_H();
}

static void W25Q64_WaitBusy(void) {
    uint32_t timeout = 100000;
    CS_L();
    SPI_SwapByte(0x05); // 读状态寄存器 1
    while ((SPI_SwapByte(0xFF) & 0x01) && timeout) {
        timeout--; // 必须加超时，防止未插模块导致永久死锁！
    }
    CS_H();
}

void W25Q64_Init(void) {
    CS_H();   // 1. 必须先拉高片选！
    SCK_L();  // 时钟空闲为低   
    MOSI_H();
    HAL_Delay(20); // 2. 核心！在 CS 为高电平状态下硬延时 20ms，让 Flash 完成内部上电复位！

    // 3. 发送唤醒指令 (0xAB: Release from Power-down)，防止芯片出厂处于休眠态
    CS_L();
    SPI_SwapByte(0xAB);
    CS_H();
    HAL_Delay(1);
}

// 读取 JEDEC ID
uint32_t W25Q64_ReadID(void) {
    uint32_t id = 0;
    
    // 读取前先确保 CS 经历了一次“从高到低”的干净跳变
    CS_H();
    SPI_Delay();
    CS_L();
    
    SPI_SwapByte(0x9F); // 读 JEDEC ID 命令
    id |= ((uint32_t)SPI_SwapByte(0xFF) << 16);
    id |= ((uint32_t)SPI_SwapByte(0xFF) << 8);
    id |= ((uint32_t)SPI_SwapByte(0xFF));
    
    CS_H(); // 拉高释放
    return id;
}

// 擦除第 0 扇区 (4KB)
static void W25Q64_EraseSector0(void) {
    W25Q64_WriteEnable();
    W25Q64_WaitBusy();
    CS_L();
    SPI_SwapByte(0x20); // 扇区擦除指令
    SPI_SwapByte(0x00); SPI_SwapByte(0x00); SPI_SwapByte(0x00); // 地址 0x000000
    CS_H();
    W25Q64_WaitBusy(); // 等待擦除完成 (约 45~400ms)
}

// 保存 4 位密码（带魔数 + 校验和）
void W25Q64_SavePassword(const uint8_t *pwd4) {
    W25Q64_EraseSector0(); // NOR Flash 写入前必须先擦除！

    W25Q64_WriteEnable();
    CS_L();
    SPI_SwapByte(0x02); // 页写入指令
    SPI_SwapByte(0x00); SPI_SwapByte(0x00); SPI_SwapByte(0x00); // 地址 0x000000

    uint8_t sum = PWD_MAGIC0 + PWD_MAGIC1;
    SPI_SwapByte(PWD_MAGIC0);
    SPI_SwapByte(PWD_MAGIC1);
    for (int i = 0; i < 4; i++) {
        SPI_SwapByte(pwd4[i]);
        sum += pwd4[i];
    }
    SPI_SwapByte(sum); // 第 7 个字节写入校验和
    CS_H();
    W25Q64_WaitBusy();
}

// 开机读取密码（三层校验防坏块与断电）
void W25Q64_LoadPassword(uint8_t *pwd4) {
    uint8_t buf[7];
    CS_L();
    SPI_SwapByte(0x03); // 读数据指令
    SPI_SwapByte(0x00); SPI_SwapByte(0x00); SPI_SwapByte(0x00);
    for (int i = 0; i < 7; i++) {
        buf[i] = SPI_SwapByte(0xFF);
    }
    CS_H();

    // 1. 校验魔数 (0xA5 0x5A)
    if (buf[0] == PWD_MAGIC0 && buf[1] == PWD_MAGIC1) {
        uint8_t sum = buf[0] + buf[1] + buf[2] + buf[3] + buf[4] + buf[5];
        // 2. 校验和匹配，且每位密码都在 0~9 合法区间
        if (sum == buf[6] && buf[2] < 10 && buf[3] < 10 && buf[4] < 10 && buf[5] < 10) {
            pwd4[0] = buf[2];
            pwd4[1] = buf[3];
            pwd4[2] = buf[4];
            pwd4[3] = buf[5];
            return; // 验证完全通过，使用读取到的密码！
        }
    }

    // 若校验失败（首次开机未初始化或数据损坏），回退到默认密码 1234
    pwd4[0] = 1; pwd4[1] = 2; pwd4[2] = 3; pwd4[3] = 4;
}