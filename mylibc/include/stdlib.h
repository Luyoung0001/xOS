#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>  /* size_t */

/* 字符串转数值 */
int atoi(const char *str);
long atol(const char *str);
long strtol(const char *str, char **endptr, int base);
unsigned long strtoul(const char *str, char **endptr, int base);

/* 绝对值 */
int abs(int n);
long labs(long n);

/* 最小/最大值宏 */
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#endif /* _STDLIB_H */
