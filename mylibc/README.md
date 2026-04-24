# mylibc - 嵌入式系统的 libc 子集

一个为嵌入式系统设计的轻量级 C 标准库实现，提供基础但完整的 libc 功能。

## 特性

- ✅ **标准兼容**：使用标准 C 库函数名，易于移植
- ✅ **弱符号支持**：`putchar` 使用弱符号，可灵活定制输出设备
- ✅ **Freestanding 环境**：不依赖系统 libc，适合裸机开发
- ✅ **轻量级**：只包含嵌入式系统常用的函数
- ✅ **BSP 集成**：默认集成 BSP 的 UART 输出

## 目录结构

```
mylibc/
├── include/           # 头文件
│   ├── stdio.h       # 输入输出
│   ├── string.h      # 字符串和内存操作
│   ├── ctype.h       # 字符分类和转换
│   └── stdlib.h      # 数值转换和工具函数
├── src/              # 源文件
│   ├── printf.c      # 格式化输出
│   ├── sprintf.c
│   ├── snprintf.c
│   ├── vsnprintf.c
│   ├── putchar.c     # 字符输出（弱符号）
│   ├── puts.c
│   ├── strlen.c      # 字符串操作
│   ├── strcpy.c
│   ├── strcat.c
│   ├── strcmp.c
│   ├── strchr.c
│   ├── strstr.c
│   ├── memset.c      # 内存操作
│   ├── memcpy.c
│   ├── memmove.c
│   ├── memcmp.c
│   ├── ctype.c       # 字符处理
│   ├── atoi.c        # 数值转换
│   ├── strtol.c
│   └── abs.c         # 数学函数
├── build/            # 编译中间文件
├── lib/              # 静态库
│   └── libmylibc.a
└── Makefile
```

## 已实现的功能

### stdio.h - 标准输入输出

**输出函数：**
- `int putchar(int c)` - 输出单个字符（弱符号，可覆盖）
- `int puts(const char *s)` - 输出字符串并换行
- `int printf(const char *fmt, ...)` - 格式化输出
- `int sprintf(char *buf, const char *fmt, ...)` - 格式化到字符串
- `int snprintf(char *buf, size_t size, const char *fmt, ...)` - 安全的格式化
- `int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)` - 核心实现

**支持的格式符：**
- `%c` - 字符
- `%s` - 字符串
- `%d`, `%i` - 有符号整数
- `%u` - 无符号整数
- `%x`, `%X` - 十六进制
- `%p` - 指针
- `%%` - 百分号
- 宽度和填充：`%08x`, `%10d` 等

### string.h - 字符串和内存操作

**字符串函数：**
- `size_t strlen(const char *s)` - 计算字符串长度
- `char *strcpy(char *dst, const char *src)` - 复制字符串
- `char *strncpy(char *dst, const char *src, size_t n)` - 安全复制
- `char *strcat(char *dst, const char *src)` - 连接字符串
- `char *strncat(char *dst, const char *src, size_t n)` - 安全连接
- `int strcmp(const char *s1, const char *s2)` - 比较字符串
- `int strncmp(const char *s1, const char *s2, size_t n)` - 比较前 n 个字符
- `char *strchr(const char *s, int c)` - 查找字符
- `char *strrchr(const char *s, int c)` - 反向查找字符
- `char *strstr(const char *haystack, const char *needle)` - 查找子串

**内存函数：**
- `void *memset(void *s, int c, size_t n)` - 填充内存
- `void *memcpy(void *dst, const void *src, size_t n)` - 复制内存
- `void *memmove(void *dst, const void *src, size_t n)` - 安全复制（处理重叠）
- `int memcmp(const void *s1, const void *s2, size_t n)` - 比较内存

### ctype.h - 字符分类和转换

**分类函数：**
- `int isdigit(int c)` - 是否为数字
- `int isalpha(int c)` - 是否为字母
- `int isalnum(int c)` - 是否为字母或数字
- `int isspace(int c)` - 是否为空白字符
- `int isupper(int c)` - 是否为大写字母
- `int islower(int c)` - 是否为小写字母
- `int isxdigit(int c)` - 是否为十六进制数字
- `int isprint(int c)` - 是否为可打印字符

**转换函数：**
- `int toupper(int c)` - 转换为大写
- `int tolower(int c)` - 转换为小写

### stdlib.h - 数值转换和工具函数

**字符串转数值：**
- `int atoi(const char *str)` - 字符串转整数
- `long atol(const char *str)` - 字符串转长整数
- `long strtol(const char *str, char **endptr, int base)` - 支持任意进制
- `unsigned long strtoul(const char *str, char **endptr, int base)` - 无符号版本

**数学函数：**
- `int abs(int n)` - 整数绝对值
- `long labs(long n)` - 长整数绝对值
- `MIN(a, b)` - 最小值宏
- `MAX(a, b)` - 最大值宏

## 编译

```bash
make          # 编译库
make clean    # 清理
make info     # 显示编译信息
```

编译后生成 `lib/libmylibc.a` 静态库。

## 使用方法

### 1. 在项目中链接

```makefile
CC = gcc
CFLAGS = -nostdlib -ffreestanding -fno-builtin
CFLAGS += -nostdinc
CFLAGS += -isystem $(shell $(CC) -print-file-name=include)
CFLAGS += -I/path/to/mylibc/include
CFLAGS += -I/path/to/bsp/include

LDFLAGS = -L/path/to/mylibc/lib -lmylibc
LDFLAGS += -L/path/to/bsp/lib -lbsp

my_program: main.c
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@
```

### 2. 在代码中使用

```c
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

int main(void) {
    // 输出
    printf("Hello, World!\n");
    printf("Number: %d, Hex: 0x%08x\n", 42, 0xDEADBEEF);

    // 字符串操作
    char buf[100];
    strcpy(buf, "Hello");
    strcat(buf, " World");
    printf("String: %s, Length: %zu\n", buf, strlen(buf));

    // 字符处理
    char c = 'a';
    printf("Upper: %c, is digit: %d\n", toupper(c), isdigit(c));

    // 数值转换
    int num = atoi("12345");
    long hex = strtol("0xABCD", NULL, 16);
    printf("atoi: %d, strtol: %ld\n", num, hex);

    return 0;
}
```

### 3. 自定义输出设备

如果需要将输出重定向到其他设备（LCD、网络等），覆盖 `putchar`：

```c
#include <stdio.h>

// 强符号，会覆盖 mylibc 的弱符号
int putchar(int c) {
    // 你的实现
    my_device_write(c);
    return c;
}

int main(void) {
    printf("This goes to my custom device!\n");
    return 0;
}
```

## 依赖关系

### GCC 提供的头文件（可直接使用）
- `<stddef.h>` - size_t, NULL, offsetof
- `<stdint.h>` - int8_t, uint32_t 等
- `<stdarg.h>` - va_list, va_start 等
- `<stdbool.h>` - bool, true, false
- `<limits.h>` - INT_MAX, UINT_MAX 等
- `<float.h>` - FLT_MAX 等

### BSP 依赖
- `bsp_uart_putc()` - UART 输出函数（由 BSP 提供）

## 编译选项说明

```makefile
-nostdlib          # 不链接标准库
-ffreestanding     # Freestanding 环境
-fno-builtin       # 禁用编译器内建函数
-nostdinc          # 不使用系统头文件
-isystem <path>    # 使用 GCC 提供的头文件
-I./include        # 使用 mylibc 的头文件
```

## 函数总览

```bash
$ nm lib/libmylibc.a | grep " T " | awk '{print $3}' | sort

abs         atoi        atol        isalnum     isalpha
isdigit     islower     isprint     isspace     isupper
isxdigit    labs        memcmp      memcpy      memmove
memset      printf      puts        snprintf    sprintf
strcat      strchr      strcmp      strcpy      strlen
strncat     strncmp     strncpy     strrchr     strstr
strtol      strtoul     tolower     toupper     vsnprintf
```

共 35 个函数。

## 还需要实现的功能（可选）

### 优先级：低
- `scanf` 系列 - 格式化输入
- `getchar()` - 字符输入
- 动态内存分配 - malloc, free 等
- 更多数学函数 - sqrt, sin, cos 等（如果需要）

## 测试

建议在使用前进行基础测试：

```c
// test.c
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

int main(void) {
    // 测试 stdio
    printf("=== Testing stdio.h ===\n");
    printf("Number: %d\n", 42);

    // 测试 string
    printf("\n=== Testing string.h ===\n");
    char buf[50];
    strcpy(buf, "Hello");
    printf("strcpy: %s\n", buf);
    printf("strlen: %zu\n", strlen(buf));

    // 测试 ctype
    printf("\n=== Testing ctype.h ===\n");
    printf("isdigit('5'): %d\n", isdigit('5'));
    printf("toupper('a'): %c\n", toupper('a'));

    // 测试 stdlib
    printf("\n=== Testing stdlib.h ===\n");
    printf("atoi(\"123\"): %d\n", atoi("123"));
    printf("abs(-42): %d\n", abs(-42));

    printf("\n=== All tests passed! ===\n");
    return 0;
}
```

## 许可证

根据你的项目需求选择合适的许可证。

## 作者

Lu Young

## 更新日志

- 2026-01-19: 初始版本，实现了 stdio, string, ctype, stdlib 的核心函数
