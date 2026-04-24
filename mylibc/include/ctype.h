#ifndef _CTYPE_H
#define _CTYPE_H

/* 字符分类函数 */
int isdigit(int c);   /* 是否为数字 0-9 */
int isalpha(int c);   /* 是否为字母 a-z, A-Z */
int isalnum(int c);   /* 是否为字母或数字 */
int isspace(int c);   /* 是否为空白字符 */
int isupper(int c);   /* 是否为大写字母 */
int islower(int c);   /* 是否为小写字母 */
int isxdigit(int c);  /* 是否为十六进制数字 */
int isprint(int c);   /* 是否为可打印字符 */

/* 字符转换函数 */
int toupper(int c);   /* 转换为大写 */
int tolower(int c);   /* 转换为小写 */

#endif /* _CTYPE_H */
