# ============================================================================
# bsp/Makefile - BSP 静态库
# ============================================================================
#
# 编译 BSP (Board Support Package) 静态库
# 提供硬件抽象层：UART, SD, PS2 等
#
# ============================================================================

LIB_NAME := libbsp.a
ASM_SRCS := start.S
DEPS     := mylibc   # uart_display_printf 需要 mylibc 的头文件

include ../scripts/common.mk
