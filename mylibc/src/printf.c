#include <stdio.h>
#include <stdarg.h>

/*
 * printf - 格式化输出到标准输出
 *
 * 依赖 putchar() 实现
 */
int printf(const char *fmt, ...) {
    char buf[256]; /* 临时缓冲区 */
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    /* 输出到 putchar */
    for (int i = 0; buf[i] != '\0'; i++) {
        putchar(buf[i]);
    }

    return ret;
}
