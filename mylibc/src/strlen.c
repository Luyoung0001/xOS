#include <string.h>

/*
 * strlen - 计算字符串长度
 *
 * 返回字符串 s 的长度（不包括结尾的 '\0'）
 */
size_t strlen(const char *s) {
    const char *p = s;
    while (*p) {
        p++;
    }
    return p - s;
}
