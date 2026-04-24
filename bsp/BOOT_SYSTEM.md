# LoongArch32R SoC 启动系统设计文档

## 概述

本文档描述 LoongArch32R SoC 的完整启动流程。系统采用两级启动方案：
1. **第一级：Bootloader（BRAM）** - 存储在 FPGA 内部 BRAM 中的启动代码
2. **第二级：用户程序（DDR）** - 存储在 SD 卡中，由 Bootloader 加载到 DDR 执行

---

## 启动流程

### 1. 上电复位 (Power-On Reset)

```
CPU Reset PC = 0x20000000 (BRAM 起始地址)
    ↓
BRAM (Boot ROM) 开始执行 Bootloader
```

- **CPU 复位向量**：`0x20000000` (修改自原来的 `0x00000000`)
- **BRAM 地址范围**：`0x20000000 - 0x20001FFF` (8KB)
- **BRAM 内容**：Bootloader 代码和数据

### 2. Bootloader 执行流程

```
┌─────────────────────────────────────────────────────────────┐
│ 第一阶段：等待 SD 卡初始化                                  │
├─────────────────────────────────────────────────────────────┤
│ 1. 初始化栈指针 ($sp = 0x20002000)                         │
│ 2. 点亮 LED0 表示 Bootloader 启动                          │
│ 3. 轮询 SD_STATUS_REG，等待 init_done 标志                │
│ 4. SD 卡初始化完成后，点亮 LED0+LED1                       │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│ 第二阶段：从 SD 卡加载用户程序到 DDR                       │
├─────────────────────────────────────────────────────────────┤
│ 变量初始化：                                                │
│   - $s0 = 0x00000000  (DDR 写入地址)                       │
│   - $s1 = 1024        (SD 卡起始扇区)                      │
│   - $s2 = 0x00400000  (剩余字节数：4MB)                    │
│                                                             │
│ 循环加载扇区：                                              │
│   For each sector (512 bytes):                             │
│     1. 等待 SD 卡空闲                                       │
│     2. 写入扇区地址到 SD_SEC_ADDR_REG                      │
│     3. 写入 0x1 到 SD_CTRL_REG 启动读取                    │
│     4. 读取 512 字节：                                      │
│        - 等待 data_valid 标志                              │
│        - 从 SD_DATA_REG 读取 1 字节                        │
│        - 写入到 DDR ($s0++)                                │
│     5. 等待 read_done 标志                                 │
│     6. 扇区号 +1，继续下一个扇区                           │
│                                                             │
│ 加载参数：                                                  │
│   - 起始扇区：1024 (0x400)                                 │
│   - 加载大小：4MB (8192 扇区)                              │
│   - 目标地址：DDR 0x00000000                               │
└─────────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────────┐
│ 第三阶段：跳转到用户程序                                    │
├─────────────────────────────────────────────────────────────┤
│ 1. 熄灭所有 LED                                            │
│ 2. 加载跳转地址：$t0 = 0x00000000                         │
│ 3. 执行跳转：jr $t0                                        │
│ 4. 用户程序从 DDR 0x00000000 开始执行                     │
└─────────────────────────────────────────────────────────────┘
```

### 3. LED 指示

Bootloader 通过 LED 指示启动状态：

| LED 状态 | 含义 |
|---------|------|
| LED0 亮 | Bootloader 已启动 |
| LED0+LED1 亮 | SD 卡初始化完成，开始加载 |
| 全灭 | 加载完成，即将跳转到用户程序 |
| 闪烁 | 错误（进入 error_loop） |

---

## 内存映射

### 系统内存布局

```
0x00000000 ┌─────────────────────────────┐
           │                             │
           │         DDR Memory          │
           │    (用户程序执行区域)        │
           │                             │
           └─────────────────────────────┘

0x1C000000 ┌─────────────────────────────┐
           │       Boot ROM (BRAM)       │
           │     8KB Bootloader Code     │
0x1C001FFF └─────────────────────────────┘

0x1FD0F000 ┌─────────────────────────────┐
           │     Configuration Regs      │
           │    (LED, Timer, etc.)       │
0x1FD0F06F └─────────────────────────────┘

0x1FD0F070 ┌─────────────────────────────┐
           │      SD Card Registers      │
0x1FD0F07F └─────────────────────────────┘
```

### SD 卡寄存器映射

| 地址 | 名称 | 类型 | 描述 |
|------|------|------|------|
| 0x1FD0F070 | SD_CTRL_REG | W | SD 控制寄存器<br>bit0: start_read (写 1 启动读取，自动清零) |
| 0x1FD0F074 | SD_STATUS_REG | R | SD 状态寄存器<br>bit0: init_done (初始化完成)<br>bit1: busy (忙碌)<br>bit2: data_valid (数据有效)<br>bit3: read_done (读取完成) |
| 0x1FD0F078 | SD_SEC_ADDR_REG | R/W | SD 扇区地址寄存器 (32位) |
| 0x1FD0F07C | SD_DATA_REG | R | SD 数据寄存器 (8位) |

---

## SD 卡布局

### 扇区分配

```
扇区 0        ┌─────────────────────────────┐
              │                             │
              │    Reserved Area (512KB)    │
              │  (MBR, Partition Table,     │
              │   File System Metadata)     │
              │                             │
扇区 1023     └─────────────────────────────┘

扇区 1024     ┌─────────────────────────────┐
              │                             │
              │     User Program Area       │
              │      (BIN file data)        │
              │                             │
              │   Loaded by Bootloader to   │
              │     DDR 0x00000000          │
              │                             │
              │   Max size: 4MB (8192 扇区) │
              │                             │
扇区 9215     └─────────────────────────────┘

扇区 9216+    ┌─────────────────────────────┐
              │                             │
              │       Free Space            │
              │  (For FAT32 filesystem,     │
              │   additional data, etc.)    │
              │                             │
              └─────────────────────────────┘
```

**说明**：
- 扇区大小：512 字节
- 用户程序起始扇区：**1024** (offset 512KB)
- 为什么从扇区 1024 开始？
  - 扇区 0-1023 (前 512KB) 保留给 MBR、分区表、文件系统元数据
  - 避免破坏 SD 卡的分区结构
  - 方便与 FAT32 文件系统共存

---

## 使用指南

### 1. 构建 Bootloader

```bash
cd bootloader/

# 编译 bootloader
make

# 生成的文件：
#   - bootloader.elf  : ELF 可执行文件
#   - bootloader.bin  : 原始二进制文件
#   - bootloader.mem  : 内存初始化文件（用于 FPGA）
#   - bootloader.lst  : 反汇编列表

# 安装到 SoC 构建目录
make install
```

**重要**：`bootloader.mem` 会被 `boot_rom.v` 通过 `$readmemh()` 加载到 BRAM 中。

### 2. 编译用户程序

用户程序需要编译为 **BIN 格式**（不是 ELF）：

```bash
cd software/your_program/

# 编译用户程序
loongarch32r-linux-gnusf-gcc -o program.elf main.c ...

# 转换为 BIN 格式
loongarch32r-linux-gnusf-objcopy -O binary program.elf program.bin

# 检查大小（不要超过 4MB）
ls -lh program.bin
```

**重要**：
- 用户程序链接地址必须是 `0x00000000`（DDR 起始地址）
- 使用 BIN 格式而非 ELF，因为：
  - BIN 格式是纯数据，直接加载即可
  - ELF 需要解析头部，bootloader 太小无法实现
  - Bootloader 只做简单的数据搬运

### 3. 写入 SD 卡

使用提供的工具将用户程序写入 SD 卡：

```bash
cd bootloader/

# 查看 SD 卡设备（假设是 /dev/sdb）
lsblk

# 写入用户程序（需要 root 权限）
sudo python3 sd_write.py /dev/sdb ../software/your_program/program.bin

# 输出示例：
# ======================================================================
# SD Card Write Tool
# ======================================================================
# Device:        /dev/sdb
# Binary file:   program.bin
# File size:     65536 bytes (64.00 KB)
# Start sector:  1024 (offset: 524288 bytes)
# Sectors used:  128
# End sector:    1151
# ======================================================================
#
# WARNING: This will write 128 sectors to /dev/sdb!
# Are you sure you want to continue? (yes/no): yes
#
# Writing 65536 bytes to /dev/sdb at offset 524288...
# ...
# Write completed successfully!
```

**注意事项**：
- **务必确认设备名称正确**（使用 `lsblk` 查看）
- 错误的设备名可能导致数据丢失
- 程序写入到扇区 1024，不会影响 SD 卡的 FAT32 分区（如果有）

### 4. 构建和烧录 FPGA

```bash
cd SoC/

# 确保 bootloader.mem 已复制到 scripts/ 目录
ls scripts/bootloader.mem

# 运行 Vivado 综合和实现
make build

# 烧录到 FPGA
make program
```

### 5. 上板运行

1. 将 SD 卡插入 FPGA 板
2. 上电或复位
3. 观察 LED 指示：
   - LED0 亮：Bootloader 启动
   - LED0+LED1 亮：SD 卡初始化完成
   - 全灭：跳转到用户程序
4. 用户程序开始运行

---

## 调试指南

### 问题：LED 一直停留在 LED0

**原因**：SD 卡初始化失败

**排查**：
1. 检查 SD 卡是否正确插入
2. 检查 SD 卡引脚连接（CLK, CS, MISO, MOSI）
3. 检查时钟频率配置（初始化时应为 250kHz）
4. 使用示波器/逻辑分析仪查看 SPI 信号

### 问题：LED0+LED1 长时间不灭

**原因**：SD 卡读取失败或超时

**排查**：
1. 确认用户程序已写入 SD 卡扇区 1024
2. 检查 SD 卡是否支持 SDHC（需要支持 16GB）
3. 增加超时时间（修改 bootloader.S 中的延时循环）
4. 在仿真中查看 SD 读取时序

### 问题：LED 闪烁

**原因**：进入错误处理循环 (error_loop)

**排查**：
1. 当前 bootloader 没有显式跳转到 error_loop
2. 如果看到闪烁，可能是用户程序导致的
3. 检查用户程序的链接地址和代码逻辑

### 问题：跳转后无响应

**原因**：用户程序加载或执行错误

**排查**：
1. 确认用户程序链接地址为 0x00000000
2. 确认 BIN 文件大小不超过 4MB
3. 检查用户程序的栈指针初始化
4. 使用仿真器检查 DDR 内容是否正确加载
5. 检查用户程序的中断向量表配置

---

## 文件清单

### Bootloader 相关

| 文件 | 路径 | 描述 |
|------|------|------|
| bootloader.S | bootloader/ | Bootloader 汇编源代码 |
| boot_rom.v | IP/boot_rom.v | BRAM 模块（Verilog） |
| bin2mem.py | bootloader/ | BIN 转 MEM 工具 |
| sd_write.py | bootloader/ | SD 卡写入工具 |
| Makefile | bootloader/ | 构建系统 |

### SoC 硬件

| 文件 | 路径 | 描述 |
|------|------|------|
| soc_top.v | SoC/top/ | SoC 顶层模块 |
| pre_IFU.v | IP/myCPU/ | CPU 复位向量配置 |
| sd_ctrl.v | IP/SD/ | SD 卡控制器 |
| confreg_a7lite.v | IP/CONFREG/ | 配置寄存器（含 SD 寄存器） |

### 文档

| 文件 | 路径 | 描述 |
|------|------|------|
| BOOT_SYSTEM.md | bsp/ | 本文档 |
| SD_CARD_DESIGN.md | bsp/ | SD 卡硬件设计文档 |

---

## 常见问题 (FAQ)

### Q1: 为什么不用 ELF 格式？

**A**: Bootloader 只有 8KB 空间，无法实现完整的 ELF 解析器。ELF 格式包含：
- ELF 头（52 字节）
- 程序头表（Program Headers）
- 节头表（Section Headers）
- 符号表、重定位表等元数据

BIN 格式是纯二进制数据，只需简单的内存复制即可加载。

### Q2: 为什么加载大小限制为 4MB？

**A**: 这是 Bootloader 的硬编码限制（`DDR_LOAD_SIZE = 0x00400000`）。原因：
- 平衡启动速度和容量
- 4MB 对于大多数嵌入式程序足够
- 如果需要更大程序，可以修改 `bootloader.S` 中的 `DDR_LOAD_SIZE`

### Q3: 能否从 FAT32 文件系统加载？

**A**: 当前 Bootloader 不支持。原因：
- FAT32 解析需要更多代码空间
- 直接扇区访问更简单可靠
- 如需 FAT32 支持，建议实现两级 Bootloader

### Q4: SD 卡写入会破坏现有分区吗？

**A**: 不会。写入从扇区 1024 开始，前 512KB 是保留区域，不会影响：
- MBR（扇区 0）
- 分区表（扇区 0）
- FAT32 第一个分区通常从扇区 2048 开始

但建议：
- 使用专用 SD 卡
- 重要数据请备份

### Q5: 如何在仿真中测试 Bootloader？

**A**: 需要修改仿真环境：
1. 创建 SD 卡仿真模型
2. 预加载用户程序到仿真 SD 卡
3. 模拟 SD 卡初始化和读取时序
4. 监控 BRAM 和 DDR 数据传输

或者：
- 直接在 DDR 仿真中预加载用户程序
- 跳过 Bootloader 阶段

---

## 性能分析

### 启动时间估算

假设 SD 卡工作频率 = 25MHz（正常模式）：

1. **SD 卡初始化**：~100ms（硬件自动完成）
2. **数据传输时间**：
   - 单扇区读取：512 字节
   - SPI 传输：512 * 8 / 25MHz ≈ 164μs
   - 加上开销（命令、响应）：~200μs/扇区
3. **加载 4MB 数据**：
   - 8192 扇区 × 200μs ≈ 1.64 秒

**总启动时间**：约 **1.7 秒**

### 优化建议

如果需要更快启动：
1. 提高 SD 卡时钟频率（最高 50MHz）
2. 使用 4-bit SD 模式（需要修改硬件）
3. 减小加载大小（如果程序较小）
4. 使用 SPI Flash 代替 SD 卡

---

## 扩展与改进

### 可能的改进方向

1. **支持多种启动源**
   - SPI Flash
   - UART 下载
   - 以太网 TFTP

2. **实现二级 Bootloader**
   - 第一级：8KB BRAM，加载第二级到 DDR
   - 第二级：功能完整，支持 FAT32、校验和、压缩等

3. **添加校验功能**
   - CRC32 校验
   - 防止加载损坏的程序

4. **支持程序更新**
   - OTA (Over-The-Air) 更新
   - A/B 双系统切换

5. **安全启动**
   - 数字签名验证
   - 加密程序镜像

---

## 参考资料

- [LoongArch32 架构手册](https://loongson.github.io/LoongArch-Documentation/)
- [SD Card Physical Layer Specification](https://www.sdcard.org/downloads/pls/)
- [SPI Mode SD Card Interface](https://openlabpro.com/guide/interfacing-microcontrollers-with-sd-card/)

---

## 修订历史

| 版本 | 日期 | 作者 | 说明 |
|------|------|------|------|
| 1.0 | 2026-01-14 | Claude | 初始版本 |

---

## 联系方式

如有问题或建议，请联系项目维护者。
