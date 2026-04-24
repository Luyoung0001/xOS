#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>

/* 辅助函数：输出无符号整数到缓冲区 */
static int buf_print_uint(char **pbuf, char *end, uint32_t num, int base, int width, char pad) {
    char tmp[32];
    int i = 0;
    int count = 0;

    /* 转换数字 */
    if (num == 0) {
        tmp[i++] = '0';
    } else {
        while (num > 0) {
            int digit = num % base;
            tmp[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
            num /= base;
        }
    }

    /* 填充 */
    while (i < width) {
        if (*pbuf < end) {
            **pbuf = pad;
            (*pbuf)++;
        }
        count++;
        width--;
    }

    /* 逆序输出 */
    while (i > 0) {
        if (*pbuf < end) {
            **pbuf = tmp[--i];
            (*pbuf)++;
        } else {
            i--;
        }
        count++;
    }

    return count;
}

/* 辅助函数：输出有符号整数到缓冲区 */
static int buf_print_int(char **pbuf, char *end, int32_t num, int width, char pad) {
    int count = 0;

    if (num < 0) {
        if (*pbuf < end) {
            **pbuf = '-';
            (*pbuf)++;
        }
        count++;
        if (width > 0) width--;
        num = -num;
    }

    count += buf_print_uint(pbuf, end, (uint32_t)num, 10, width, pad);
    return count;
}

/*
 * vsnprintf - 格式化输出到缓冲区（带大小限制）
 *
 * buf: 目标缓冲区
 * size: 缓冲区大小（包括 '\0'）
 * fmt: 格式字符串
 * ap: 可变参数列表
 *
 * 返回：如果不截断应该写入的字符数（不包括 '\0'）
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    char *p = buf;
    char *end = buf + size - 1; /* 保留一个字节给 '\0' */
    int count = 0;

    if (size == 0) {
        return 0;
    }

    while (*fmt) {
        if (*fmt != '%') {
            if (p < end) {
                *p++ = *fmt;
            }
            count++;
            fmt++;
            continue;
        }

        fmt++; /* 跳过 '%' */

        /* 解析宽度和填充 */
        char pad = ' ';
        int width = 0;

        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }

        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* 解析格式符 */
        switch (*fmt) {
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (p < end) {
                *p++ = c;
            }
            count++;
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) {
                s = "(null)";
            }
            while (*s) {
                if (p < end) {
                    *p++ = *s;
                }
                s++;
                count++;
            }
            break;
        }
        case 'd':
        case 'i': {
            int32_t num = va_arg(ap, int32_t);
            count += buf_print_int(&p, end, num, width, pad);
            break;
        }
        case 'u': {
            uint32_t num = va_arg(ap, uint32_t);
            count += buf_print_uint(&p, end, num, 10, width, pad);
            break;
        }
        case 'x':
        case 'X': {
            uint32_t num = va_arg(ap, uint32_t);
            count += buf_print_uint(&p, end, num, 16, width, pad);
            break;
        }
        case 'p': {
            uintptr_t ptr = (uintptr_t)va_arg(ap, void *);
            if (p < end) *p++ = '0';
            if (p < end) *p++ = 'x';
            count += 2;
            count += buf_print_uint(&p, end, ptr, 16, 8, '0');
            break;
        }
        case '%': {
            if (p < end) {
                *p++ = '%';
            }
            count++;
            break;
        }
        default:
            if (p < end) *p++ = '%';
            if (p < end) *p++ = *fmt;
            count += 2;
            break;
        }

        fmt++;
    }

    /* 添加结尾的 '\0' */
    *p = '\0';

    return count;
}
