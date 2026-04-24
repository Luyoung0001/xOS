/*
 * linker_qemu.ld - LoongArch32 QEMU virt 机器链接脚本
 *
 * QEMU virt 机器内存布局:
 *   0x00100000 - 0x00200000 : FDT (设备树，QEMU 自动放置)
 *   0x00200000 - ...        : 可用于程序
 *   0x1FE001E0              : UART (ns16550a)
 *
 * 我们把程序放在 0x00200000 开始，避免与 FDT 冲突
 */

OUTPUT_ARCH(loongarch)
ENTRY(_start)

/* 程序起始地址 (避开 FDT) */
_PROGRAM_START = 0x00200000;

MEMORY
{
    RAM (rwx) : ORIGIN = 0x00200000, LENGTH = 256M
}

/* 栈大小 */
_stack_size = 8M;

SECTIONS
{
    /* ======== 启动代码 ======== */
    . = _PROGRAM_START;
    .text.entry : {
        _text_start = .;
        KEEP(*(.text.start))
    } > RAM

    /* ======== 异常/中断入口 ======== */
    .exception ALIGN(64) : {
        __exception_start = .;
        KEEP(*(.exception))
        *(.exception.*)
        __exception_end = .;
    } > RAM

    /* ======== 代码段 ======== */
    .text ALIGN(16) : {
        *(.text)
        *(.text.*)
        _text_end = .;
    } > RAM

    /* ======== 只读数据段 ======== */
    .rodata ALIGN(16) : {
        _rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
        _rodata_end = .;
    } > RAM

    /* ======== 已初始化数据段 ======== */
    .data ALIGN(16) : {
        _data_start = .;
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        _data_end = .;
    } > RAM

    /* ======== 未初始化数据段 ======== */
    .bss ALIGN(16) : {
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        _bss_end = .;
    } > RAM

    /* ======== 程序结束标记 ======== */
    . = ALIGN(16);
    _end = .;

    /* ======== 堆区域 ======== */
    _heap_start = .;
    _heap_end = 0x0E000000;  /* 堆结束于栈底 */

    /* ======== 栈区域 ======== */
    _stack_bottom = 0x0E000000;
    _stack_top = 0x0F000000;

    /* ======== Framebuffer (QEMU 无 HDMI，用内存模拟) ======== */
    _framebuf_base = 0x0F000000;
    _framebuf_end = 0x10000000;

    /* 丢弃不需要的段 */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}

/* 导出符号 */
PROVIDE(__bss_start = _bss_start);
PROVIDE(__bss_end = _bss_end);
PROVIDE(__stack_top = _stack_top);
PROVIDE(__heap_start = _heap_start);
PROVIDE(__heap_end = _heap_end);
PROVIDE(__framebuf_base = _framebuf_base);
PROVIDE(__framebuf_end = _framebuf_end);
