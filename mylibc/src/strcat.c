#include <string.h>

/*
 * strcat - 连接字符串
 *
 * 将 src 追加到 dst 的末尾
 * 返回 dst
 */
char *strcat(char *dst, const char *src) {
    char *ret = dst;

    /* 找到 dst 的末尾 */
    while (*dst) {
        dst++;
    }

    /* 复制 src */
    while ((*dst++ = *src++)) {
        /* 复制直到遇到 '\0' */
    }

    return ret;
}

/*
 * strncat - 安全地连接字符串
 *
 * 最多追加 n 个字符从 src 到 dst
 * 总是添加 '\0'
 */
char *strncat(char *dst, const char *src, size_t n) {
    char *ret = dst;

    /* 找到 dst 的末尾 */
    while (*dst) {
        dst++;
    }

    /* 复制最多 n 个字符 */
    while (n-- && (*dst = *src++)) {
        dst++;
    }

    /* 确保以 '\0' 结尾 */
    *dst = '\0';

    return ret;
}
