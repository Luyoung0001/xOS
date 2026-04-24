#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>  /* size_t */

/* 字符串长度 */
size_t strlen(const char *s);

/* 字符串复制 */
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);

/* 字符串连接 */
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);

/* 字符串比较 */
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);

/* 字符串搜索 */
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);

/* 内存操作 */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);

#endif /* _STRING_H */
