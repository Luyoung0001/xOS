#include <string.h>

/*
 * memcmp - 比较内存
 *
 * 比较 s1 和 s2 指向的内存的前 n 个字节
 * 返回值：
 *   < 0  如果 s1 < s2
 *   = 0  如果 s1 == s2
 *   > 0  如果 s1 > s2
 */
int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }

    return 0;
}
