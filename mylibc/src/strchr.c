#include <string.h>

/*
 * strchr - 在字符串中查找字符
 *
 * 返回字符 c 在字符串 s 中第一次出现的位置
 * 如果未找到返回 NULL
 */
char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }

    /* 检查是否查找 '\0' */
    if (*s == (char)c) {
        return (char *)s;
    }

    return NULL;
}

/*
 * strrchr - 在字符串中反向查找字符
 *
 * 返回字符 c 在字符串 s 中最后一次出现的位置
 * 如果未找到返回 NULL
 */
char *strrchr(const char *s, int c) {
    const char *last = NULL;

    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }

    /* 检查是否查找 '\0' */
    if (*s == (char)c) {
        return (char *)s;
    }

    return (char *)last;
}
