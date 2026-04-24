# ============================================================================
# xOS Top-level Makefile
# 目标：编译并在 QEMU(LoongArch virt) 运行 xos_pro_max
# ============================================================================

ROOT_DIR   := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
APP_DIR    := $(ROOT_DIR)/software/xos_pro_max
BSP_DIR    := $(ROOT_DIR)/bsp
MYLIBC_DIR := $(ROOT_DIR)/mylibc
QEMU_ELF   := $(APP_DIR)/build/xos.elf
COMPILE_DB := $(ROOT_DIR)/compile_commands.json

BEAR := $(shell command -v bear 2>/dev/null)

.PHONY: all qemu_build qemu_run qemu_gui qemu_debug clean

all: qemu_build

qemu_build:
	@echo "========================================"
	@echo " 编译 QEMU 版本 xOS Pro Max..."
	@echo "========================================"
	@rm -f $(COMPILE_DB)
	@rm -rf $(BSP_DIR)/lib $(BSP_DIR)/build
	@rm -rf $(MYLIBC_DIR)/lib $(MYLIBC_DIR)/build
	@rm -rf $(APP_DIR)/build
	@if [ -n "$(BEAR)" ]; then \
		echo "[INFO] 使用 bear 生成 compile_commands.json"; \
		bear --output $(COMPILE_DB) -- $(MAKE) -C $(MYLIBC_DIR) QEMU_RUN=1 --no-print-directory; \
		bear --append --output $(COMPILE_DB) -- $(MAKE) -C $(BSP_DIR) QEMU_RUN=1 --no-print-directory; \
		bear --append --output $(COMPILE_DB) -- $(MAKE) -C $(APP_DIR) QEMU_RUN=1 --no-print-directory; \
	else \
		echo "[WARN] bear 未安装，跳过 compile_commands.json 生成"; \
		$(MAKE) -C $(MYLIBC_DIR) QEMU_RUN=1 --no-print-directory; \
		$(MAKE) -C $(BSP_DIR) QEMU_RUN=1 --no-print-directory; \
		$(MAKE) -C $(APP_DIR) QEMU_RUN=1 --no-print-directory; \
	fi
	@echo "========================================"
	@echo " QEMU 版本编译完成"
	@echo " ELF: $(QEMU_ELF)"
	@echo "========================================"

qemu_run: qemu_build
	@echo "========================================"
	@echo " 启动 QEMU (串口模式)"
	@echo " 退出: Ctrl+C"
	@echo "========================================"
	qemu-system-loongarch64 \
		-machine virt \
		-cpu la464 \
		-m 2G \
		-device bochs-display,xres=640,yres=480 \
		-display none \
		-serial stdio \
		-kernel $(QEMU_ELF)

qemu_gui: qemu_build
	@echo "========================================"
	@echo " 启动 QEMU (图形模式)"
	@echo "========================================"
	qemu-system-loongarch64 \
		-machine virt \
		-cpu la464 \
		-m 2G \
		-device bochs-display,xres=640,yres=480 \
		-display gtk \
		-serial stdio \
		-kernel $(QEMU_ELF)

qemu_debug: qemu_build
	@echo "========================================"
	@echo " 启动 QEMU 调试模式"
	@echo " GDB: target remote :1234"
	@echo "========================================"
	qemu-system-loongarch64 \
		-machine virt \
		-cpu la464 \
		-m 2G \
		-nographic \
		-kernel $(QEMU_ELF) \
		-S -s

clean:
	rm -rf $(BSP_DIR)/build $(BSP_DIR)/lib
	rm -rf $(MYLIBC_DIR)/build $(MYLIBC_DIR)/lib
	rm -rf $(APP_DIR)/build
	rm -f $(COMPILE_DB)
	@echo "已清理构建产物"
