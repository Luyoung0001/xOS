#include <string.h>

/*
 * memset - 填充内存
 *
 * 将 s 指向的内存的前 n 个字节设置为 c
 * 返回 s
 */
void *memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    unsigned char value = (unsigned char)c;

    while (n--) {
        *p++ = value;
    }

    return s;
}
