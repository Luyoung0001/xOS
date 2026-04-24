#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

/*
 * strtol - 将字符串转换为长整数（支持不同进制）
 *
 * str: 要转换的字符串
 * endptr: 如果不为 NULL，保存转换结束位置的指针
 * base: 进制 (2-36)，如果为 0 则自动检测（0x 表示十六进制，0 表示八进制）
 */
long strtol(const char *str, char **endptr, int base) {
    long result = 0;
    int sign = 1;
    const char *p = str;

    /* 跳过空白字符 */
    while (isspace(*p)) {
        p++;
    }

    /* 处理正负号 */
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }

    /* 自动检测进制 */
    if (base == 0) {
        if (*p == '0') {
            if (p[1] == 'x' || p[1] == 'X') {
                base = 16;
                p += 2;
            } else {
                base = 8;
                p++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        /* 跳过 0x 前缀 */
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
        }
    }

    /* 检查进制是否有效 */
    if (base < 2 || base > 36) {
        if (endptr) {
            *endptr = (char *)str;
        }
        return 0;
    }

    /* 转换数字 */
    while (*p) {
        int digit;

        if (isdigit(*p)) {
            digit = *p - '0';
        } else if (isalpha(*p)) {
            digit = toupper(*p) - 'A' + 10;
        } else {
            break;
        }

        if (digit >= base) {
            break;
        }

        result = result * base + digit;
        p++;
    }

    if (endptr) {
        *endptr = (char *)p;
    }

    return sign * result;
}

/*
 * strtoul - 将字符串转换为无符号长整数
 */
unsigned long strtoul(const char *str, char **endptr, int base) {
    unsigned long result = 0;
    const char *p = str;

    /* 跳过空白字符 */
    while (isspace(*p)) {
        p++;
    }

    /* 跳过可选的 '+' */
    if (*p == '+') {
        p++;
    }

    /* 自动检测进制 */
    if (base == 0) {
        if (*p == '0') {
            if (p[1] == 'x' || p[1] == 'X') {
                base = 16;
                p += 2;
            } else {
                base = 8;
                p++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        /* 跳过 0x 前缀 */
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
        }
    }

    /* 检查进制是否有效 */
    if (base < 2 || base > 36) {
        if (endptr) {
            *endptr = (char *)str;
        }
        return 0;
    }

    /* 转换数字 */
    while (*p) {
        int digit;

        if (isdigit(*p)) {
            digit = *p - '0';
        } else if (isalpha(*p)) {
            digit = toupper(*p) - 'A' + 10;
        } else {
            break;
        }

        if (digit >= base) {
            break;
        }

        result = result * base + digit;
        p++;
    }

    if (endptr) {
        *endptr = (char *)p;
    }

    return result;
}
