#include <stdio.h>
#include <stdarg.h>

/*
 * sprintf - 格式化输出到字符串（无大小限制）
 *
 * 警告：不检查缓冲区大小，可能导致溢出
 * 建议使用 snprintf 代替
 */
int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    /* 使用一个足够大的值，避免 SIZE_MAX 导致指针溢出 */
    ret = vsnprintf(buf, 0x7FFFFFFF, fmt, ap);
    va_end(ap);

    return ret;
}
