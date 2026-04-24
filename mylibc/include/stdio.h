#ifndef _STDIO_H
#define _STDIO_H

#include <stddef.h>  /* GCC 提供的 size_t */
#include <stdarg.h>  /* GCC 提供的 va_list */

/* 基础字符输出函数 */
int putchar(int c);
int puts(const char *s);

/* 格式化输出函数 */
int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

#endif /* _STDIO_H */
