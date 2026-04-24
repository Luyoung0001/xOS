#include <string.h>

/*
 * strcmp - 比较字符串
 *
 * 返回值：
 *   < 0  如果 s1 < s2
 *   = 0  如果 s1 == s2
 *   > 0  如果 s1 > s2
 */
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/*
 * strncmp - 比较字符串的前 n 个字符
 *
 * 最多比较 n 个字符
 */
int strncmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) {
        return 0;
    }

    while (n-- && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }

    if (n == (size_t)-1) {
        return 0;
    }

    return *(unsigned char *)s1 - *(unsigned char *)s2;
}
