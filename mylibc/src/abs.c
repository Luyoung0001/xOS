#include <stdlib.h>

/*
 * abs - 返回整数的绝对值
 */
int abs(int n) {
    return (n < 0) ? -n : n;
}

/*
 * labs - 返回长整数的绝对值
 */
long labs(long n) {
    return (n < 0) ? -n : n;
}
