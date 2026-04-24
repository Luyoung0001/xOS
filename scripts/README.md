# 编译系统脚本

## 概述

这个目录包含项目的通用编译引擎和链接脚本，所有模块（BSP、mylibc、用户程序）都使用这些脚本进行编译。

## 文件

### common.mk - 统一编译引擎

**功能：**
- 自动识别模块类型（静态库 vs 可执行文件）
- 智能管理依赖库（只在需要时编译）
- 自动符号剔除（--gc-sections）
- 支持 Freestanding 环境
- 生成 ELF、BIN、反汇编文件

**使用方法：**

编译静态库：
```makefile
LIB_NAME := libmylib.a
include ../scripts/common.mk
```

编译可执行文件：
```makefile
APP_NAME := myapp
DEPS     := mylibc bsp
include ../../scripts/common.mk
```

### linker.ld - 链接脚本

定义程序在内存中的布局，包括：
- 代码段 (.text)
- 只读数据段 (.rodata)
- 数据段 (.data)
- BSS 段 (.bss)
- 栈和堆

## 可用命令

### 对于静态库项目
```bash
make           # 编译库
make clean     # 清理
make info      # 显示编译变量
make help      # 显示帮助
```

### 对于可执行文件项目
```bash
make           # 智能编译（检查依赖库是否存在）
make force-all # 强制重新编译所有（依赖库 + 程序）
make clean     # 清理用户程序
make clean-all # 清理所有（包括依赖库）
make info      # 显示编译变量
make help      # 显示帮助
```

## 编译流程

### 默认编译（make）
1. 检查依赖库是否存在
2. 不存在才编译依赖库
3. 编译用户代码
4. 链接并剔除未使用符号

**优势：** 快速增量编译，适合日常开发

### 强制重新编译（make force-all）
1. 清理所有依赖库
2. 重新编译所有依赖库
3. 清理并重新编译用户代码
4. 链接并剔除未使用符号

**优势：** 确保所有代码最新，适合发布前

## 性能对比

```
智能编译（依赖库已存在）：0.033s
强制重新编译所有：        0.445s

速度提升：13.5 倍
```

## 架构设计

```
scripts/common.mk (编译引擎)
    ↓
├─→ bsp/         (使用 ../scripts/common.mk)
├─→ mylibc/      (使用 ../scripts/common.mk)
└─→ software/*/  (使用 ../../scripts/common.mk)
```

**设计原则：**
- **独立性** - scripts 独立于任何模块
- **通用性** - 同一套规则适用于所有模块
- **智能化** - 自动处理依赖和优化
- **高效性** - 增量编译，只编译必要的部分

## 路径计算

common.mk 会自动计算以下路径：
```makefile
SCRIPTS_DIR := <common.mk 所在目录>
ROOT_DIR    := <项目根目录>
BSP_DIR     := <ROOT_DIR>/bsp
MODULE_DIR  := <当前模块目录>
BUILD_DIR   := <MODULE_DIR>/build
```

## 添加新的依赖库

如果要添加新的库（如 myfs），需要在 common.mk 中添加：

1. 库路径映射：
```makefile
LIB_PATH_myfs := $(ROOT_DIR)/myfs/lib/libmyfs.a
```

2. 头文件路径：
```makefile
ifneq ($(filter myfs,$(DEPS)),)
CFLAGS += -I$(ROOT_DIR)/myfs/include
endif
```

3. 编译规则：
```makefile
force-myfs:
	@if echo "$(DEPS)" | grep -q "myfs"; then \
		echo "[BUILD-DEP] libmyfs"; \
		$(MAKE) -C $(ROOT_DIR)/myfs --no-print-directory; \
	fi

deps: check-deps
check-deps: ... force-myfs
```

## 更多信息

详细文档请参考项目根目录的 `BUILD_SYSTEM.md`。
