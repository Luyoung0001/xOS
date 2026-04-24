#include <stdio.h>

/*
 * puts - 输出字符串并换行
 *
 * 标准行为：输出字符串，然后自动添加换行符
 * 依赖 putchar() 实现
 */
int puts(const char *s) {
    if (s == NULL) {
        return -1;
    }

    /* 输出字符串 */
    while (*s) {
        putchar(*s++);
    }

    /* 自动添加换行 */
    putchar('\n');

    return 0;
}
