# software/xos_pro_max/Makefile - xOS Pro Max with NES Emulator
APP_NAME := xos
ASM_SRCS := src/trap_entry.S src/sched_switch.S

# LiteNES 源文件
LITENES_SRCS := src/litenes/common.c \
                src/litenes/cpu.c \
                src/litenes/cpu-addressing.c \
                src/litenes/fce.c \
                src/litenes/memory.c \
                src/litenes/mmc.c \
                src/litenes/ppu.c \
                src/litenes/psg.c \
                src/litenes/mario-rom.c \
                src/litenes/am_adapter.c \
                src/litenes/jit_simple.c

# 所有 C 源文件（自动发现 + LiteNES）
C_SRCS := $(wildcard src/*.c) $(LITENES_SRCS)

DEPS := bsp mylibc   # 注意：BSP 依赖 mylibc，所以 bsp 在前

# 添加 LiteNES 头文件搜索路径
EXTRA_CFLAGS := -Iinclude/litenes

# 支持 SIMU=1 条件编译
ifdef SIMU
SIMULATION := 1
endif

include ../../scripts/common.mk
