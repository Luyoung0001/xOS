# ============================================================================
# common.mk - LoongArch32 通用编译引擎
# ============================================================================
#
# 使用方法:
#
# 1. 编译静态库:
#    LIB_NAME := libbsp.a
#    C_SRCS   := $(wildcard src/*.c)
#    ASM_SRCS := start.S
#    include path/to/common.mk
#
# 2. 编译可执行文件:
#    APP_NAME := hello
#    C_SRCS   := hello.c
#    DEPS     := mylib bsp          # 依赖的库 (会自动查找)
#    include path/to/common.mk
#
# ============================================================================

# 获取 common.mk 所在目录 (即 scripts/)
SCRIPTS_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
ROOT_DIR    := $(SCRIPTS_DIR)/..
BSP_DIR     := $(ROOT_DIR)/bsp

# 当前模块目录
MODULE_DIR  := $(CURDIR)
BUILD_DIR   := $(MODULE_DIR)/build

# ============================================================================
# 工具链
# ============================================================================
CROSS_COMPILE ?= loongarch32r-linux-gnusf-
CC      := $(CROSS_COMPILE)gcc
AR      := $(CROSS_COMPILE)ar
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
SIZE    := $(CROSS_COMPILE)size

# ============================================================================
# 编译选项
# ============================================================================
# 获取 GCC 内建头文件路径
GCC_INCLUDE_DIR := $(shell $(CC) -print-file-name=include 2>/dev/null || echo "")

CFLAGS  := -march=loongarch32r -mabi=ilp32s
CFLAGS  += -O2
CFLAGS  += -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -nostdlib -nostartfiles -ffreestanding -fno-builtin
CFLAGS  += -fno-stack-protector
CFLAGS  += -fno-pic
# CFLAGS  += -ffunction-sections -fdata-sections  # 每个函数/数据单独一个段

# Freestanding 环境：不使用系统头文件，只用 GCC 内建和我们自己的
CFLAGS  += -nostdinc
ifneq ($(GCC_INCLUDE_DIR),)
CFLAGS  += -isystem $(GCC_INCLUDE_DIR)  # 允许使用 GCC 的 stdint.h, stdarg.h 等
endif

# 仿真模式 (跳过硬件等待循环)
ifdef SIMULATION
CFLAGS  += -DSIMULATION
endif

# QEMU 模式 (使用 QEMU virt 机器运行)
ifdef QEMU_RUN
CFLAGS  += -DQEMU_RUN
endif

ASFLAGS := -mabi=ilp32s

# 链接选项：--gc-sections 剔除未使用的段（函数/数据）
# --print-gc-sections 可以显示被剔除的段（调试用）
LDFLAGS := -nostdlib -static --gc-sections
# LDFLAGS += --print-gc-sections  # 取消注释可查看剔除的符号

# ============================================================================
# 自动添加头文件路径
# ============================================================================
# 总是包含 BSP 头文件
CFLAGS  += -I$(BSP_DIR)/include
ASFLAGS += -I$(BSP_DIR)/include

# 如果有本地 include 目录
ifneq ($(wildcard $(MODULE_DIR)/include),)
CFLAGS  += -I$(MODULE_DIR)/include
ASFLAGS += -I$(MODULE_DIR)/include
endif

# 允许模块通过 EXTRA_CFLAGS / EXTRA_ASFLAGS 追加自定义选项
ifdef EXTRA_CFLAGS
CFLAGS  += $(EXTRA_CFLAGS)
endif
ifdef EXTRA_ASFLAGS
ASFLAGS += $(EXTRA_ASFLAGS)
endif

# 如果依赖 mylibc，添加其头文件路径
ifneq ($(filter mylibc,$(DEPS)),)
CFLAGS  += -I$(ROOT_DIR)/mylibc/include
ASFLAGS += -I$(ROOT_DIR)/mylibc/include
endif

# ============================================================================
# 源文件处理
# ============================================================================
# 如果未指定源文件，自动收集
ifeq ($(C_SRCS),)
  ifneq ($(wildcard $(MODULE_DIR)/src/*.c),)
    C_SRCS := $(wildcard $(MODULE_DIR)/src/*.c)
  else
    C_SRCS := $(wildcard $(MODULE_DIR)/*.c)
  endif
endif

# 转换为目标文件
C_OBJS   := $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(C_SRCS)))
ASM_OBJS := $(patsubst %.S,$(BUILD_DIR)/%.o,$(notdir $(ASM_SRCS)))
ALL_OBJS := $(ASM_OBJS) $(C_OBJS)

# VPATH: 源文件搜索路径
VPATH := $(sort $(dir $(C_SRCS) $(ASM_SRCS))) $(MODULE_DIR)

# ============================================================================
# 依赖库路径
# ============================================================================
# 库搜索路径映射
LIB_PATH_bsp    := $(BSP_DIR)/lib/libbsp.a
LIB_PATH_mylibc := $(ROOT_DIR)/mylibc/lib/libmylibc.a

# 收集依赖库文件
DEP_LIBS := $(foreach dep,$(DEPS),$(LIB_PATH_$(dep)))

# 调试信息
ifneq ($(VERBOSE),)
$(info [INFO] DEPS = $(DEPS))
$(info [INFO] DEP_LIBS = $(DEP_LIBS))
endif

# ============================================================================
# 目标判断: 静态库 or 可执行文件
# ============================================================================

.PHONY: all clean bear deps help

ifdef LIB_NAME
# ==================== 静态库模式 ====================
TARGET := $(MODULE_DIR)/lib/$(LIB_NAME)

all: $(TARGET)
	@echo ""
	@echo "========================================"
	@echo " 静态库编译完成: $(LIB_NAME)"
	@echo " 输出: $(TARGET)"
	@echo "========================================"

$(TARGET): $(ALL_OBJS) | $(dir $(TARGET))
	@echo "[AR] $(LIB_NAME)"
	@$(AR) rcs $@ $(ALL_OBJS)

$(dir $(TARGET)):
	@mkdir -p $@

else ifdef APP_NAME
# ==================== 可执行文件模式 ====================
ifdef QEMU_RUN
LDSCRIPT := $(SCRIPTS_DIR)/linker_qemu.ld
else
LDSCRIPT := $(SCRIPTS_DIR)/linker.ld
endif
TARGET   := $(BUILD_DIR)/$(APP_NAME).elf
TARGET_BIN := $(BUILD_DIR)/$(APP_NAME).bin
TARGET_ASM := $(BUILD_DIR)/$(APP_NAME).txt

# 链接顺序很重要：用户代码 -> 高层库 -> 底层库
# 依赖在后面，被依赖的在前面反转
LINK_LIBS := $(DEP_LIBS)

all: deps $(TARGET) $(TARGET_BIN) $(TARGET_ASM)
	@echo ""
	@echo "========================================"
	@echo " 可执行文件编译完成: $(APP_NAME)"
	@echo " ELF: $(TARGET)"
	@echo " BIN: $(TARGET_BIN)"
	@echo " TXT: $(TARGET_ASM)"
	@echo "========================================"
	@$(SIZE) $(TARGET)

# ============================================================================
# 智能依赖管理：只在必要时编译依赖库
# ============================================================================
# make           - 默认：检查依赖库是否存在，不存在才编译
# make force-all - 强制重新编译所有依赖库和用户程序
# ============================================================================

.PHONY: deps check-deps build-deps force-all force-bsp force-mylibc clean-libs

# 默认行为：智能检查依赖
deps: check-deps

# 检查依赖库是否存在，不存在才编译
check-deps:
	@if echo "$(DEPS)" | grep -q "bsp"; then \
		if [ ! -f "$(LIB_PATH_bsp)" ]; then \
			echo "[BUILD-DEP] libbsp (not found)"; \
			$(MAKE) -C $(BSP_DIR) --no-print-directory; \
		fi; \
	fi
	@if echo "$(DEPS)" | grep -q "mylibc"; then \
		if [ ! -f "$(LIB_PATH_mylibc)" ]; then \
			echo "[BUILD-DEP] libmylibc (not found)"; \
			$(MAKE) -C $(ROOT_DIR)/mylibc --no-print-directory; \
		fi; \
	fi

# 强制编译所有依赖（清理并重新编译）
build-deps: clean-libs force-bsp force-mylibc

clean-libs:
	@if echo "$(DEPS)" | grep -q "bsp"; then \
		rm -rf $(BSP_DIR)/lib $(BSP_DIR)/build; \
		echo "[CLEAN-DEP] libbsp"; \
	fi
	@if echo "$(DEPS)" | grep -q "mylibc"; then \
		rm -rf $(ROOT_DIR)/mylibc/lib $(ROOT_DIR)/mylibc/build; \
		echo "[CLEAN-DEP] libmylibc"; \
	fi

force-bsp:
	@if echo "$(DEPS)" | grep -q "bsp"; then \
		echo "[BUILD-DEP] libbsp"; \
		$(MAKE) -C $(BSP_DIR) --no-print-directory; \
	fi

force-mylibc:
	@if echo "$(DEPS)" | grep -q "mylibc"; then \
		echo "[BUILD-DEP] libmylibc"; \
		$(MAKE) -C $(ROOT_DIR)/mylibc --no-print-directory; \
	fi

# 强制重新编译所有（依赖库 + 用户程序）
force-all: clean build-deps
	@echo ""
	@echo "========================================="
	@echo " 强制重新编译所有模块..."
	@echo "========================================="
	@$(MAKE) all --no-print-directory

$(TARGET): $(ALL_OBJS) $(DEP_LIBS) | $(BUILD_DIR)
	@echo "[LD] $(APP_NAME).elf"
	@$(LD) $(LDFLAGS) -T $(LDSCRIPT) -o $@ $(ALL_OBJS) $(LINK_LIBS)

$(TARGET_BIN): $(TARGET)
	@echo "[BIN] $(APP_NAME).bin"
	@$(OBJCOPY) -O binary $< $@

$(TARGET_ASM): $(TARGET)
	@echo "[TXT] $(APP_NAME).txt"
	@$(OBJDUMP) -D -h -t $< > $(TARGET_ASM)


else
$(error "必须定义 LIB_NAME 或 APP_NAME")
endif

# ============================================================================
# 通用规则
# ============================================================================

$(BUILD_DIR):
	@mkdir -p $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	@echo "[CC] $(notdir $<)"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.S | $(BUILD_DIR)
	@echo "[AS] $(notdir $<)"
	@$(CC) $(ASFLAGS) -c -o $@ $<

clean:
	@echo "[CLEAN] $(BUILD_DIR)"
	@rm -rf $(BUILD_DIR)
	@rm -f /home/luyoung/gra_Design/trace.fst /home/luyoung/gra_Design/trace.txt /home/luyoung/gra_Design/mem_trace.txt
ifdef LIB_NAME
	@rm -rf $(MODULE_DIR)/lib
endif


# 生成 compile_commands.json
bear: clean
	@mkdir -p $(BUILD_DIR)
	bear -- $(MAKE) all
	@echo "compile_commands.json 已生成"

# 帮助信息
help:
	@echo "========================================="
	@echo " common.mk 编译引擎 - 可用命令"
	@echo "========================================="
ifdef LIB_NAME
	@echo "make           - 编译静态库"
	@echo "make clean     - 清理编译产物"
else ifdef APP_NAME
	@echo "make           - 编译用户程序（智能检查依赖）"
	@echo "make force-all - 强制重新编译所有（依赖库+程序）"
	@echo "make clean     - 清理用户程序"
endif
	@echo "make bear      - 生成 compile_commands.json"
	@echo "make info      - 显示编译变量信息"
	@echo "========================================="

# 调试信息
info:
	@echo "MODULE_DIR : $(MODULE_DIR)"
	@echo "BSP_DIR    : $(BSP_DIR)"
	@echo "ROOT_DIR   : $(ROOT_DIR)"
	@echo "CFLAGS     : $(CFLAGS)"
	@echo "C_SRCS     : $(C_SRCS)"
	@echo "ASM_SRCS   : $(ASM_SRCS)"
	@echo "DEPS       : $(DEPS)"
	@echo "DEP_LIBS   : $(DEP_LIBS)"
	@echo "VPATH      : $(VPATH)"
