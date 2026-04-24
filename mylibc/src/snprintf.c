#include <stdio.h>
#include <stdarg.h>

/*
 * snprintf - 格式化输出到字符串（带大小限制）
 */
int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);

    return ret;
}
