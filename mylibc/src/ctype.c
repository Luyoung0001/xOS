#include <ctype.h>

/*
 * isdigit - 检查是否为数字字符 (0-9)
 */
int isdigit(int c) {
    return (c >= '0' && c <= '9');
}

/*
 * isalpha - 检查是否为字母 (a-z, A-Z)
 */
int isalpha(int c) {
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

/*
 * isalnum - 检查是否为字母或数字
 */
int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

/*
 * isspace - 检查是否为空白字符
 * 包括：空格、制表符、换行符、回车符、换页符、垂直制表符
 */
int isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' ||
            c == '\r' || c == '\f' || c == '\v');
}

/*
 * isupper - 检查是否为大写字母 (A-Z)
 */
int isupper(int c) {
    return (c >= 'A' && c <= 'Z');
}

/*
 * islower - 检查是否为小写字母 (a-z)
 */
int islower(int c) {
    return (c >= 'a' && c <= 'z');
}

/*
 * isxdigit - 检查是否为十六进制数字 (0-9, a-f, A-F)
 */
int isxdigit(int c) {
    return (isdigit(c) ||
            (c >= 'a' && c <= 'f') ||
            (c >= 'A' && c <= 'F'));
}

/*
 * isprint - 检查是否为可打印字符 (包括空格)
 */
int isprint(int c) {
    return (c >= ' ' && c <= '~');
}

/*
 * toupper - 将小写字母转换为大写
 * 如果不是小写字母，返回原值
 */
int toupper(int c) {
    if (islower(c)) {
        return c - 'a' + 'A';
    }
    return c;
}

/*
 * tolower - 将大写字母转换为小写
 * 如果不是大写字母，返回原值
 */
int tolower(int c) {
    if (isupper(c)) {
        return c - 'A' + 'a';
    }
    return c;
}
