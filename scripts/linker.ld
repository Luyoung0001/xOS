/*
 * linker.ld - LoongArch32 裸机链接脚本
 *
 * 内存布局 (512MB DDR):
 *   0x00000000 - 0x1CFFFFFF : RAM (464MB) - 代码+数据+堆
 *   0x1D000000 - 0x1EFFFFFF : Stack (32MB)
 *   0x1F000000 - 0x1FFFFFFF : Framebuffer (16MB)
 *   0x20000000 - 0x20000FFF : Boot ROM (4KB, BRAM)
 *
 * 地址空间分配:
 *   .text.entry: 0x00000000  启动代码 (.text.start)
 *   .exception : 0x00000040  异常/中断入口 (64字节对齐)
 *   .text      : 紧随其后    其余代码
 *   .rodata    : 紧随 .text  只读数据
 *   .data      : 紧随 .rodata 已初始化数据
 *   .bss       : 紧随 .data   未初始化数据
 *   heap       : _end 之后    堆 (向上增长至 0x1CFFFFFF)
 *   stack      : 0x1D000000 - 0x1EFFFFFF (向下增长, 32MB)
 */

OUTPUT_ARCH(loongarch)
ENTRY(_start)

/* 内存定义 */
MEMORY
{
    RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 464M
}

/* 栈大小 */
_stack_size = 32M;

SECTIONS
{
    /* ======== 启动代码 (必须在 0x00) ======== */
    . = 0x00000000;
    .text.entry : {
        _text_start = .;
        KEEP(*(.text.start))    /* 启动代码在最前面 */
        . = 0x40;               /* 填充到 0x40，为 .exception 预留空间 */
    } > RAM

    /* ======== 异常/中断入口 (紧随 .text.entry，地址 0x40) ======== */
    .exception : {
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
    _heap_end = 0x1D000000;  /* 堆结束于栈底 */
    _heap_size = _heap_end - _heap_start;

    /* ======== 栈区域 ======== */
    /* 栈: 0x1D000000 - 0x1EFFFFFF (32MB, 向下增长) */
    _stack_bottom = 0x1D000000;
    _stack_top = 0x1F000000;

    /* ======== Framebuffer 区域 ======== */
    _framebuf_base = 0x1F000000;
    _framebuf_end = 0x20000000;

    /* 丢弃不需要的段 */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}

/* 导出符号供程序使用 */
PROVIDE(__bss_start = _bss_start);
PROVIDE(__bss_end = _bss_end);
PROVIDE(__stack_top = _stack_top);
PROVIDE(__heap_start = _heap_start);
PROVIDE(__heap_end = _heap_end);
PROVIDE(__framebuf_base = _framebuf_base);
PROVIDE(__framebuf_end = _framebuf_end);
