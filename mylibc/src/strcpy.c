#include <string.h>

/*
 * strcpy - 复制字符串
 *
 * 将 src 复制到 dst（包括 '\0'）
 * 返回 dst
 * 警告：不检查缓冲区大小，可能溢出，建议使用 strncpy
 */
char *strcpy(char *dst, const char *src) {
    char *ret = dst;
    while ((*dst++ = *src++)) {
        /* 复制直到遇到 '\0' */
    }
    return ret;
}

/*
 * strncpy - 安全地复制字符串
 *
 * 最多复制 n 个字符从 src 到 dst
 * 如果 src 长度小于 n，剩余部分填充 '\0'
 * 如果 src 长度大于等于 n，dst 可能不会以 '\0' 结尾
 */
char *strncpy(char *dst, const char *src, size_t n) {
    char *ret = dst;
    size_t i;

    /* 复制字符 */
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }

    /* 填充剩余部分为 '\0' */
    for (; i < n; i++) {
        dst[i] = '\0';
    }

    return ret;
}
