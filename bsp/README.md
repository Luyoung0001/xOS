## bsp

### 地址划分

```C
 // CSR.CRMD.DA = 1 (直接地址模式)
  // CSR.CRMD.PG = 0 (关闭分页)

  //==========================================================================
  // 主存储器
  //==========================================================================
  #define RAM_BASE        0x00000000  // DDR3/SRAM 起始
  #define RAM_END         0x1CFFFFFF  // 464MB (代码+数据+堆)
  // Stack
  #define STACK_TOP       0x1EFFFFFF
  #define STACK_BOTT      0x1D000000  // 32MB
  // Framebuffer 放在 RAM 末尾
  #define FRAMEBUF_BASE   0x1F000000
  #define FRAMEBUF_SIZE   0x01000000  // 16MB (双缓冲 1080P)
  #define FRAMEBUF_END    0x1FFFFFFF

  //==========================================================================
  // Boot ROM (BRAM 实现)
  //==========================================================================
  #define BOOTROM_BASE    0x20000000  // Boot ROM 起始
  #define BOOTROM_SIZE    0x00002000  // 8KB BRAM
  #define BOOTROM_END     0x20001FFF  // 边界

  // ┌─────────────┬────────────┬────────────┬─────────────────┐
  // │    区域     │  起始地址  │  结束地址  │      大小       │
  // ├─────────────┼────────────┼────────────┼─────────────────┤
  // │ RAM         │ 0x00000000 │ 0x1CFFFFFF │ 464MB (非466MB) │
  // ├─────────────┼────────────┼────────────┼─────────────────┤
  // │ Stack       │ 0x1D000000 │ 0x1EFFFFFF │ 32MB ✓          │
  // ├─────────────┼────────────┼────────────┼─────────────────┤
  // │ Framebuffer │ 0x1F000000 │ 0x1FFFFFFF │ 16MB ✓          │
  // └─────────────┴────────────┴────────────┴─────────────────┘

  //==========================================================================
  // 外设 - CONFREG (0x1FD0_xxxx)
  //==========================================================================
  #define CONFREG_BASE    0x1FD00000

  // 控制寄存器
  #define CR0_ADDR        0x1FD00000  // CR0
  #define CR0_ADDR        0x1FD00004  // CR1
  #define CR0_ADDR        0x1FD00008  // CR2
  #define CR0_ADDR        0x1FD0000c  // CR3
  #define CR0_ADDR        0x1FD00010  // CR4
  #define CR0_ADDR        0x1FD00014  // CR5
  #define CR0_ADDR        0x1FD00018  // CR6
  #define CR7_ADDR        0x1FD0001C  // CR7

  // Timer
  #define TIMER_ADDR      0x1FD0E000

  // LED & Button
  #define LED_ADDR        0x1FD0F000
  #define BTN_STEP_ADDR   0x1FD0F028
  #define FREQ_ADDR       0x1FD0F030

  // GPIO
  #define GPIO_DATA_ADDR  0x1FD0F040
  #define GPIO_DIR_ADDR   0x1FD0F044
  #define GPIO_IN_ADDR    0x1FD0F048

  // PS2 Keyboard
  #define PS2_DATA_ADDR   0x1FD0F050
  #define PS2_STATUS_ADDR 0x1FD0F054
  #define PS2_CTRL_ADDR   0x1FD0F058

  //==========================================================================
  // 外设 - APB (0x1FE0_xxxx)
  //==========================================================================
  #define APB_BASE        0x1FE00000
  #define UART_BASE       0x1FE001E0  // UART 16550
```

