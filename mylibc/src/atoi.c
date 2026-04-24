#include <stdlib.h>
#include <ctype.h>

/*
 * atoi - 将字符串转换为整数
 *
 * 跳过前导空白，识别可选的正负号，然后转换数字
 * 遇到非数字字符时停止
 */
int atoi(const char *str) {
    int result = 0;
    int sign = 1;

    /* 跳过空白字符 */
    while (isspace(*str)) {
        str++;
    }

    /* 处理正负号 */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    /* 转换数字 */
    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }

    return sign * result;
}

/*
 * atol - 将字符串转换为长整数
 */
long atol(const char *str) {
    long result = 0;
    int sign = 1;

    /* 跳过空白字符 */
    while (isspace(*str)) {
        str++;
    }

    /* 处理正负号 */
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }

    /* 转换数字 */
    while (isdigit(*str)) {
        result = result * 10 + (*str - '0');
        str++;
    }

    return sign * result;
}
