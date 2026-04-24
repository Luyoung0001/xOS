# xOS

LoongArch32 裸机 xOS（`xos_pro_max`）已迁移到本仓库，包含 `bsp` 与 `mylibc`，支持在 QEMU `virt` 机型运行。

## 快速开始

```bash
make qemu_build
make qemu_run
```

## 目录说明

- `software/xos_pro_max`: xOS Pro Max 应用源码
- `bsp`: 板级支持包（UART/HDMI/PS2/NES/QEMU framebuffer）
- `mylibc`: freestanding libc 子集
- `scripts`: 通用构建脚本与 linker script
