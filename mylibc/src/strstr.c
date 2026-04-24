#include <string.h>

/*
 * strstr - 在字符串中查找子串
 *
 * 返回子串 needle 在字符串 haystack 中第一次出现的位置
 * 如果未找到返回 NULL
 * 如果 needle 为空串，返回 haystack
 */
char *strstr(const char *haystack, const char *needle) {
    const char *h, *n;

    /* 空串情况 */
    if (*needle == '\0') {
        return (char *)haystack;
    }

    /* 遍历 haystack */
    while (*haystack) {
        h = haystack;
        n = needle;

        /* 尝试匹配 */
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }

        /* 如果 needle 匹配完成 */
        if (*n == '\0') {
            return (char *)haystack;
        }

        haystack++;
    }

    return NULL;
}
