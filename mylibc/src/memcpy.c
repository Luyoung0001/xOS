#include <string.h>

/*
 * memcpy - 复制内存
 *
 * 将 src 指向的内存的前 n 个字节复制到 dst
 * 返回 dst
 * 警告：src 和 dst 不能重叠，如果重叠请使用 memmove
 */
void *memcpy(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    while (n--) {
        *d++ = *s++;
    }

    return dst;
}
