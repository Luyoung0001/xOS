# SD 卡读取系统设计文档

## 目录

1. [概述](#概述)
2. [SD 卡基础知识](#sd-卡基础知识)
3. [SPI 模式工作原理](#spi-模式工作原理)
4. [硬件设计](#硬件设计)
5. [软件驱动设计](#软件驱动设计)
6. [使用指南](#使用指南)
7. [调试建议](#调试建议)

---

## 概述

本文档详细介绍了在 LoongArch32R SoC 中实现的 SD 卡读取系统。该系统采用 **SPI 模式**通信，支持 SDHC 卡（16GB），提供只读功能，适用于 FAT32 文件系统。

### 系统特性

- **接口模式**: SPI（4线：CLK, CS, MISO, MOSI）
- **支持卡类型**: SD 2.0, SDHC (≤32GB)
- **传输速率**: 初始化 250kHz, 数据传输 25MHz
- **访问方式**: 内存映射寄存器
- **扇区大小**: 512 字节
- **功能**: 只读（Write 接口预留但未启用）

---

## SD 卡基础知识

### SD 卡类型

| 类型 | 容量范围 | 寻址方式 | 本项目支持 |
|------|---------|---------|-----------|
| SDSC | ≤2GB | 字节地址 | ✓ |
| SDHC | 2GB-32GB | 扇区地址 | ✓ (16GB) |
| SDXC | 32GB-2TB | 扇区地址 | ✓* |


### SD 卡通信模式

SD 卡支持两种通信模式：

1. **SD 模式**（原生模式）
   - 使用 4 位数据总线
   - 高速传输（最高 50MB/s）
   - 需要专用 SD 控制器

2. **SPI 模式**（兼容模式）✓
   - 使用标准 SPI 接口（4线）
   - 速度较慢但实现简单
   - **本项目采用**

---

## SPI 模式工作原理

### SPI 信号定义

```
sd_clk  (SCLK)  : 串行时钟，主机输出
sd_cs_n (CS)    : 片选，低电平有效，主机输出
sd_mosi (MOSI)  : 主机输出，从机输入（命令和数据）
sd_miso (MISO)  : 主机输入，从机输出（响应和数据）
```

### 时钟极性和相位

SD 卡 SPI 模式使用：
- **CPOL = 0**: 时钟空闲时为低电平
- **CPHA = 0**: 数据在时钟上升沿采样，下降沿变化

### SD 卡初始化序列

```
上电
  ↓
等待 74 个时钟周期（CS=1, MOSI=1）
  ↓
发送 CMD0（GO_IDLE_STATE）
  ↓ 响应 0x01
发送 CMD8（SEND_IF_COND）检测 SD 2.0
  ↓ 响应 0x01 + 4字节（电压范围）
循环发送：
  CMD55（APP_CMD）
    ↓ 响应 0x01
  ACMD41（SD_SEND_OP_COND, HCS=1）
    ↓ 响应 0x01 或 0x00
直到响应为 0x00（初始化完成）
  ↓
初始化完成，可以开始数据传输
```

### SD 卡命令格式

每条命令 6 字节：

```
Byte 0: 01xxxxxx  (起始位 + 命令号，如 CMD0 = 0x40)
Byte 1-4: 参数（32位，大端序）
Byte 5: CRC7 + 停止位
```

**示例：CMD0**
```
0x40 0x00 0x00 0x00 0x00 0x95
 ↑                        ↑
命令号=0                 CRC=0x95
```

### 读扇区流程（CMD17）

```
1. 主机发送 CMD17 + 扇区地址
   0x51 [sector_addr(32-bit)] 0xFF

2. SD 卡响应 R1（1字节）
   0x00 = 成功

3. SD 卡发送数据令牌
   0xFE = 数据块开始

4. SD 卡发送 512 字节数据
   [512 bytes data]

5. SD 卡发送 CRC16（2字节，SPI模式下忽略）
   [CRC16 high] [CRC16 low]
```

**时序图：**
```
      CMD17           R1      Token    Data (512B)   CRC
MOSI: [6 bytes] → → → → → → → → → → → → → → → → → → → →
MISO: ← ← ← ← ← [0x00] [0xFE] [byte0]...[byte511] [CRC]
CS_N: ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
      (低电平有效，持续整个传输过程)
```

---

## 硬件设计

### 系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                         SoC Top                              │
│  ┌────────────┐      ┌──────────────┐      ┌─────────────┐ │
│  │    CPU     │◄────►│   Confreg    │◄────►│  SD  Ctrl   │ │
│  │ (AXI Master)│      │ (Registers)  │      │             │ │
│  └────────────┘      └──────────────┘      └──────┬──────┘ │
│                                                    │         │
└────────────────────────────────────────────────────┼─────────┘
                                                     │
                                          ┌──────────▼──────────┐
                                          │  SD Card (SPI Mode) │
                                          │  sd_clk  sd_cs_n    │
                                          │  sd_miso sd_mosi    │
                                          └─────────────────────┘
```

### 模块层次结构

```
soc_top.v
  ├─ confreg (寄存器模块)
  │    ├─ SD 控制寄存器 (SD_CTRL_REG)
  │    ├─ SD 状态寄存器 (SD_STATUS_REG)
  │    ├─ SD 扇区地址寄存器 (SD_SEC_ADDR_REG)
  │    └─ SD 数据寄存器 (SD_DATA_REG)
  │
  └─ sd_ctrl (SD 卡控制器)
       ├─ sd_init (初始化模块)
       │    ├─ 时钟分频器 (50MHz → 250kHz)
       │    ├─ 命令发送器 (CMD0, CMD8, CMD55, ACMD41)
       │    └─ 应答接收器
       │
       └─ sd_rd (读取模块)
            ├─ 命令发送器 (CMD17)
            ├─ 数据接收器 (512字节 + CRC)
            └─ 状态机控制
```

### 硬件模块详解

#### 1. sd_init 模块

**功能**: 上电自动初始化 SD 卡

**时钟**: 250kHz（从 50MHz 分频，分频系数 = 50,000,000 / 250,000 / 2 = 100）

**状态机**:
```
IDLE → SEND_CMD0 → SEND_CMD8 → SEND_CMD55 → SEND_ACMD41 → INIT_DONE
  ↑                                  ↑______________|
  |__________________________________|
        (失败或超时重试)
```

**关键参数**:
- `POWER_ON_NUM = 5000`: 上电等待周期
- `OVER_TIME_NUM = 50000`: 超时计数（~200ms）

#### 2. sd_rd 模块

**功能**: 读取单个扇区（512字节）

**工作时钟**: 系统时钟 50MHz（通过 `assign sd_clk = ~clk` 实现）

**状态机**:
```
IDLE → WR_CMD17 → RD_DATA → RD_DONE → IDLE
       (发送命令) (接收512B)  (等待)
```

**数据接收计数**:
- `rx_data_cnt = 0-513`
  - 0: 数据令牌 (0xFE)
  - 1-512: 实际数据
  - 513-514: CRC16（忽略）

#### 3. sd_ctrl 模块

**功能**: 顶层控制器，协调初始化和读取

**信号多路复用**:
```verilog
// 根据状态选择信号源
if (!init_done)
    {sd_cs_n, sd_mosi} <= {init_sd_cs_n, init_sd_mosi};  // 初始化
else if (sd_wr_busy)
    {sd_cs_n, sd_mosi} <= {wr_sd_cs_n, wr_sd_mosi};      // 写入（未使用）
else if (sd_rd_busy)
    {sd_cs_n, sd_mosi} <= {rd_sd_cs_n, rd_sd_mosi};      // 读取
else
    {sd_cs_n, sd_mosi} <= {1'b1, 1'b1};                   // 空闲
```

### 寄存器接口

#### 寄存器映射表

| 地址 | 名称 | 位宽 | 访问 | 说明 |
|------|------|------|------|------|
| 0x1FD0F070 | SD_CTRL | 32 | W | 控制寄存器 |
| 0x1FD0F074 | SD_STATUS | 32 | R | 状态寄存器 |
| 0x1FD0F078 | SD_SEC_ADDR | 32 | R/W | 扇区地址寄存器 |
| 0x1FD0F07C | SD_DATA | 32 | R | 数据寄存器 |

#### SD_CTRL (0x1FD0F070) - 控制寄存器

```
Bit 31-1: 保留（写入忽略）
Bit 0: start_read
       写1启动读取，硬件自动在1个时钟周期后清零
```

**使用方法**:
```c
// 启动读取扇区 100
*(volatile uint32_t *)0x1FD0F078 = 100;     // 设置扇区地址
*(volatile uint32_t *)0x1FD0F070 = 0x1;     // 启动读取
// start_read 自动变为 0
```

#### SD_STATUS (0x1FD0F074) - 状态寄存器

```
Bit 31-4: 保留
Bit 3: read_done     - 读取完成（1=完成）
Bit 2: data_valid    - 当前数据有效（1=SD_DATA 中有新数据）
Bit 1: busy          - SD 卡忙（1=忙，0=空闲）
Bit 0: init_done     - 初始化完成（1=完成，0=未完成）
```

**状态转换**:
```
上电: 0x0000 (全0)
  ↓
初始化中: 0x0002 (busy=1)
  ↓
初始化完成: 0x0001 (init_done=1)
  ↓
读取中: 0x0003 (init_done=1, busy=1)
  ↓
数据到达: 0x0007 (init_done=1, busy=1, data_valid=1)
  ↓
读取完成: 0x0009 (init_done=1, read_done=1)
```

#### SD_SEC_ADDR (0x1FD0F078) - 扇区地址寄存器

```
Bit 31-0: 扇区地址（32位）
          SDHC: 扇区号（扇区0 = 物理地址0）
          SDSC: 字节地址（需要 ×512）
```

**注意**: SDHC 卡使用扇区地址，SDSC 卡使用字节地址。本设计针对 SDHC 优化。

#### SD_DATA (0x1FD0F07C) - 数据寄存器

```
Bit 31-8: 保留（读出为0）
Bit 7-0: 读取的数据字节
```

**数据有效性**: 只有当 `SD_STATUS.data_valid = 1` 时，读取才有意义。

### 引脚约束

根据 A7-Lite 开发板设计：

```tcl
# SD Card Clock
set_property PACKAGE_PIN U7 [get_ports sd_clk]
set_property IOSTANDARD LVCMOS33 [get_ports sd_clk]

# SD Card Chip Select (active low)
set_property PACKAGE_PIN Y8 [get_ports sd_cs_n]
set_property IOSTANDARD LVCMOS33 [get_ports sd_cs_n]
set_property PULLUP TRUE [get_ports sd_cs_n]

# SD Card MISO (Master In, Slave Out)
set_property PACKAGE_PIN W9 [get_ports sd_miso]
set_property IOSTANDARD LVCMOS33 [get_ports sd_miso]

# SD Card MOSI (Master Out, Slave In)
set_property PACKAGE_PIN AA8 [get_ports sd_mosi]
set_property IOSTANDARD LVCMOS33 [get_ports sd_mosi]
set_property PULLUP TRUE [get_ports sd_mosi]
```

**注意**: CS 和 MOSI 配置了上拉电阻，确保空闲时为高电平。

---

## 软件驱动设计

### 驱动文件

- **头文件**: `/software/mylib/sd.h`
- **实现文件**: `/software/mylib/sd.c`

### API 接口

#### 1. sd_init() - 初始化

```c
int sd_init(void);
```

**功能**: 等待硬件自动初始化完成

**返回值**:
- `0`: 成功
- `-1`: 超时（初始化失败）

**实现逻辑**:
```c
int sd_init(void) {
    uint32_t timeout = SD_INIT_TIMEOUT;  // 10M 周期 (~200ms)

    while (timeout--) {
        status = REG_READ(SD_STATUS_REG);
        if (status & SD_STATUS_INIT_DONE)
            return 0;  // 初始化完成
    }

    return -1;  // 超时
}
```

#### 2. sd_read_sector() - 读取单个扇区

```c
int sd_read_sector(uint32_t sector_addr, uint8_t *buffer);
```

**功能**: 读取一个扇区（512字节）

**参数**:
- `sector_addr`: 扇区地址（0-based）
- `buffer`: 输出缓冲区（至少512字节）

**返回值**:
- `0`: 成功
- `-1`: 失败

**详细流程**:

```c
int sd_read_sector(uint32_t sector_addr, uint8_t *buffer) {
    // 1. 检查初始化状态
    if (!sd_is_init())
        return -1;

    // 2. 等待空闲
    while (sd_is_busy() && timeout--)
        ;

    // 3. 写入扇区地址
    REG_WRITE(SD_SEC_ADDR_REG, sector_addr);

    // 4. 启动读取
    REG_WRITE(SD_CTRL_REG, 0x1);

    // 5. 循环读取 512 字节
    for (i = 0; i < 512; i++) {
        // 等待数据有效
        while (timeout--) {
            status = REG_READ(SD_STATUS_REG);
            if (status & SD_STATUS_DATA_VALID)
                break;
        }

        // 读取数据
        buffer[i] = REG_READ(SD_DATA_REG);
    }

    // 6. 等待读取完成
    while (timeout--) {
        status = REG_READ(SD_STATUS_REG);
        if (status & SD_STATUS_READ_DONE)
            break;
    }

    return 0;
}
```

**时序示意**:
```
CPU操作                     硬件状态
  ↓
写地址寄存器                 idle
  ↓
写控制寄存器(start=1)        busy=1, 发送CMD17
  ↓
读状态等待data_valid         接收数据
  ↓
读数据寄存器(byte 0)         data_valid=1
  ↓
读状态等待data_valid         data_valid=0, 准备下一字节
  ↓
读数据寄存器(byte 1)         data_valid=1
  ...
  (重复 512 次)
  ↓
读状态等待read_done          接收 CRC
  ↓
完成                         read_done=1, busy=0
```

#### 3. sd_read_sectors() - 读取多个扇区

```c
int sd_read_sectors(uint32_t start_sector, uint32_t num_sectors, uint8_t *buffer);
```

**功能**: 连续读取多个扇区

**实现**: 循环调用 `sd_read_sector()`

```c
for (i = 0; i < num_sectors; i++) {
    if (sd_read_sector(start_sector + i, buffer + i * 512) != 0)
        return -1;
}
```

#### 4. sd_read() - 任意字节读取

```c
int sd_read(uint32_t byte_offset, uint8_t *buffer, uint32_t length);
```

**功能**: 从任意字节偏移读取数据，自动处理扇区对齐

**算法**:
1. 计算起始扇区和扇区内偏移
2. 读取第一个部分扇区（如果有偏移）
3. 读取完整扇区
4. 读取最后一个部分扇区（如果需要）

**示例**:
```c
// 从字节地址 1000 读取 2000 字节
// 扇区 1: 字节 512-1023   (需要读 488 字节: 1000-1023)
// 扇区 2: 字节 1024-1535  (完整扇区)
// 扇区 3: 字节 1536-2047  (完整扇区)
// 扇区 4: 字节 2048-2559  (需要读 464 字节: 2048-2511)

uint8_t buf[2000];
sd_read(1000, buf, 2000);
```

### 超时机制

所有等待操作都配置了超时保护：

| 操作 | 超时值 | 时间 (50MHz) |
|------|--------|--------------|
| 初始化 | 10,000,000 周期 | ~200ms |
| 读取扇区 | 5,000,000 周期 | ~100ms |
| 等待数据 | 5,000,000 周期 | ~100ms |

**超时处理**:
```c
uint32_t timeout = SD_READ_TIMEOUT;
while (timeout--) {
    if (condition_met)
        break;
}
if (timeout == 0)
    return -1;  // 超时错误
```

---

## 使用指南

### 基础示例

#### 示例 1: 读取 MBR（主引导记录）

```c
#include "sd.h"
#include <stdio.h>

int main() {
    uint8_t mbr[512];
    int ret;

    // 初始化 SD 卡
    printf("Initializing SD card...\n");
    ret = sd_init();
    if (ret != 0) {
        printf("SD card initialization failed!\n");
        return -1;
    }
    printf("SD card initialized successfully\n");

    // 读取扇区 0（MBR）
    printf("Reading MBR (sector 0)...\n");
    ret = sd_read_sector(0, mbr);
    if (ret != 0) {
        printf("Failed to read MBR!\n");
        return -1;
    }

    // 检查 MBR 签名
    if (mbr[510] == 0x55 && mbr[511] == 0xAA) {
        printf("Valid MBR signature found!\n");

        // 打印分区表
        printf("Partition table:\n");
        for (int i = 0; i < 4; i++) {
            uint8_t *entry = &mbr[446 + i * 16];
            printf("  Partition %d: Type=0x%02X\n", i+1, entry[4]);
        }
    } else {
        printf("Invalid MBR signature\n");
    }

    return 0;
}
```

**预期输出**:
```
Initializing SD card...
SD card initialized successfully
Reading MBR (sector 0)...
Valid MBR signature found!
Partition table:
  Partition 1: Type=0x0C  (FAT32 LBA)
  Partition 2: Type=0x00  (Empty)
  Partition 3: Type=0x00  (Empty)
  Partition 4: Type=0x00  (Empty)
```

#### 示例 2: 读取 FAT32 引导扇区

```c
uint8_t boot_sector[512];
uint32_t partition_lba;

// 从 MBR 读取第一个分区的起始 LBA
partition_lba = *(uint32_t *)(&mbr[446 + 8]);  // Partition 1 LBA

// 读取分区引导扇区
sd_read_sector(partition_lba, boot_sector);

// 解析 FAT32 参数
uint16_t bytes_per_sector = *(uint16_t *)(&boot_sector[11]);
uint8_t  sectors_per_cluster = boot_sector[13];
uint32_t sectors_per_fat = *(uint32_t *)(&boot_sector[36]);

printf("FAT32 Info:\n");
printf("  Bytes per sector: %u\n", bytes_per_sector);
printf("  Sectors per cluster: %u\n", sectors_per_cluster);
printf("  Sectors per FAT: %u\n", sectors_per_fat);
```

#### 示例 3: 连续读取数据

```c
// 读取连续的 10 个扇区
uint8_t large_buffer[10 * 512];

ret = sd_read_sectors(100, 10, large_buffer);
if (ret == 0) {
    printf("Successfully read 10 sectors (5120 bytes)\n");
}
```

#### 示例 4: 任意位置读取

```c
// 读取从字节偏移 2048 开始的 1000 字节
uint8_t data[1000];

ret = sd_read(2048, data, 1000);
if (ret == 1000) {
    printf("Successfully read 1000 bytes\n");
}
```

### 集成 FatFs 文件系统

如果需要完整的文件系统支持，可以集成 FatFs 库：

#### 1. 实现 diskio 接口

```c
// diskio.c
#include "ff.h"
#include "sd.h"

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;

    if (sd_init() == 0)
        return 0;  // Success
    else
        return STA_NOINIT;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;

    if (sd_read_sectors(sector, count, buff) == 0)
        return RES_OK;
    else
        return RES_ERROR;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;

    if (sd_is_init())
        return 0;  // OK
    else
        return STA_NOINIT;
}
```

#### 2. 使用文件系统

```c
#include "ff.h"

FATFS fs;
FIL file;
FRESULT res;
UINT bytes_read;
char buffer[256];

// 挂载文件系统
res = f_mount(&fs, "", 1);
if (res != FR_OK) {
    printf("Mount failed: %d\n", res);
    return;
}

// 打开文件
res = f_open(&file, "test.txt", FA_READ);
if (res != FR_OK) {
    printf("Open failed: %d\n", res);
    return;
}

// 读取文件
res = f_read(&file, buffer, sizeof(buffer), &bytes_read);
if (res == FR_OK) {
    printf("Read %u bytes: %s\n", bytes_read, buffer);
}

// 关闭文件
f_close(&file);
```

### 错误处理

```c
int ret = sd_read_sector(sector_addr, buffer);

switch (ret) {
    case 0:
        printf("Success\n");
        break;
    case -1:
        // 可能的原因：
        // 1. SD 卡未初始化
        if (!sd_is_init()) {
            printf("Error: SD card not initialized\n");
        }
        // 2. SD 卡忙超时
        else if (sd_is_busy()) {
            printf("Error: SD card busy timeout\n");
        }
        // 3. 读取数据超时
        else {
            printf("Error: Read timeout\n");
        }
        break;
}
```

---

## 调试建议

### 硬件调试

#### 1. 检查初始化状态

```c
// 读取状态寄存器
uint32_t status = *(volatile uint32_t *)0x1FD0F074;

printf("SD Status: 0x%08X\n", status);
printf("  init_done  : %d\n", (status >> 0) & 1);
printf("  busy       : %d\n", (status >> 1) & 1);
printf("  data_valid : %d\n", (status >> 2) & 1);
printf("  read_done  : %d\n", (status >> 3) & 1);
```

**预期**: 初始化完成后 `init_done = 1`, `busy = 0`

#### 2. 使用 LED 指示

```c
#define LED_REG 0x1FD0F000

// 初始化成功，点亮 LED0
if (sd_init() == 0) {
    *(volatile uint32_t *)LED_REG = 0x1;
}

// 读取成功，点亮 LED1
if (sd_read_sector(0, buffer) == 0) {
    *(volatile uint32_t *)LED_REG |= 0x2;
}
```

#### 3. UART 调试输出

```c
void print_hex(uint8_t *data, int len) {
    for (int i = 0; i < len; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    printf("\n");
}

// 打印 MBR 前 64 字节
uint8_t mbr[512];
sd_read_sector(0, mbr);
print_hex(mbr, 64);
```

### 常见问题

#### 问题 1: 初始化超时

**现象**: `sd_init()` 返回 -1

**可能原因**:
1. SD 卡未插入或接触不良
2. 引脚连接错误
3. 电源不稳定

**调试步骤**:
1. 检查物理连接
2. 用万用表测量 SD 卡供电（应为 3.3V）
3. 检查引脚约束是否正确

#### 问题 2: 读取数据全为 0xFF

**现象**: `sd_read_sector()` 成功，但数据全为 0xFF

**可能原因**:
1. SD 卡未格式化
2. 读取了空白扇区
3. MISO 信号未连接（默认上拉为高）

**调试步骤**:
1. 在电脑上格式化 SD 卡为 FAT32
2. 用十六进制编辑器写入测试数据
3. 检查 MISO 引脚连接

#### 问题 3: 读取时间过长

**现象**: 读取 512 字节耗时超过 50ms

**可能原因**:
1. 软件轮询效率低
2. 时钟频率配置错误

**优化方案**:
1. 使用中断方式而非轮询
2. 检查 `sd_clk` 频率是否达到 25MHz

### 波形调试

使用 ILA（Integrated Logic Analyzer）或示波器观察信号：

#### 关键信号

```verilog
// 在 soc_top.v 中添加调试信号
(* mark_debug = "true" *) wire sd_clk;
(* mark_debug = "true" *) wire sd_cs_n;
(* mark_debug = "true" *) wire sd_miso;
(* mark_debug = "true" *) wire sd_mosi;
(* mark_debug = "true" *) wire sd_init_done;
(* mark_debug = "true" *) wire sd_rd_busy;
```

#### 预期波形（读取流程）

```
sd_cs_n:  ‾‾‾‾‾‾\____________________________________/‾‾‾‾‾
sd_clk:   ______/‾\__/‾\__/‾\__/‾\__/‾\__/‾\__/‾\__/‾\___
sd_mosi:  ‾‾‾‾‾‾\_[CMD17 6字节]________________________
sd_miso:  ________/‾‾‾‾\__[R1]_[0xFE]_[Data 512B]_[CRC]
                  (等待)
```

### 性能测试

```c
#include <time.h>

void benchmark() {
    uint8_t buffer[512];
    uint32_t start, end;

    // 测试单扇区读取
    start = get_cycles();
    sd_read_sector(0, buffer);
    end = get_cycles();

    printf("Single sector read: %u cycles\n", end - start);
    printf("Throughput: %.2f KB/s\n",
           512.0 * 50000000 / (end - start) / 1024);

    // 测试连续读取
    start = get_cycles();
    sd_read_sectors(0, 100, large_buffer);
    end = get_cycles();

    printf("100 sectors read: %u cycles\n", end - start);
    printf("Throughput: %.2f KB/s\n",
           51200.0 * 50000000 / (end - start) / 1024);
}
```

**预期性能**:
- 单扇区读取：~15ms（约 34 KB/s）
- 连续读取：~12ms/扇区（约 42 KB/s）

---

## 附录

### A. SD 卡命令表

| 命令 | 名称 | 参数 | 响应 | 说明 |
|------|------|------|------|------|
| CMD0 | GO_IDLE_STATE | 0 | R1 | 复位SD卡到空闲状态 |
| CMD8 | SEND_IF_COND | 0x1AA | R7 | 发送接口条件（SD 2.0） |
| CMD17 | READ_SINGLE_BLOCK | 地址 | R1 | 读取单个数据块 |
| CMD55 | APP_CMD | 0 | R1 | 下一条命令是应用命令 |
| ACMD41 | SD_SEND_OP_COND | 0x40000000 | R1 | 发送操作条件寄存器 |

### B. 响应类型

| 类型 | 长度 | 格式 | 说明 |
|------|------|------|------|
| R1 | 1 字节 | 0bxxxxxxx | 标准响应 |
| R7 | 5 字节 | R1 + 4字节 | CMD8 响应 |

**R1 位定义**:
```
Bit 7: 始终为0
Bit 6: Parameter error
Bit 5: Address error
Bit 4: Erase sequence error
Bit 3: CRC error
Bit 2: Illegal command
Bit 1: Erase reset
Bit 0: In idle state
```

### C. 参考资料

1. **SD Card Specification** (Physical Layer Simplified Specification Version 9.00)
   - https://www.sdcard.org/downloads/pls/

2. **SPI Mode Implementation**
   - 参考设计：`/docs/A7_lite_demo_35T/24_sd_rd_wr_test/`

3. **FatFs Documentation**
   - http://elm-chan.org/fsw/ff/00index_e.html

4. **本项目相关文件**
   - 硬件：`/IP/SD/`, `/SoC/top/soc_top.v`, `/IP/CONFREG/confreg_a7lite.v`
   - 软件：`/software/mylib/sd.h`, `/software/mylib/sd.c`
   - 约束：`/SoC/scripts/soc_constraints.xdc`

---

**文档版本**: v1.0
**最后更新**: 2026-01-14
**作者**: Claude Code
**项目**: LoongArch32R SoC with SD Card Support
