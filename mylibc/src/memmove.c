#include <string.h>

/*
 * memmove - 安全地复制内存
 *
 * 将 src 指向的内存的前 n 个字节复制到 dst
 * 可以处理 src 和 dst 重叠的情况
 * 返回 dst
 */
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0) {
        return dst;
    }

    if (d < s) {
        /* 从前往后复制 */
        while (n--) {
            *d++ = *s++;
        }
    } else {
        /* 从后往前复制，避免重叠覆盖 */
        d += n;
        s += n;
        while (n--) {
            *(--d) = *(--s);
        }
    }

    return dst;
}
