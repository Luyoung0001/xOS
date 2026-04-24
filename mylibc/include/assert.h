#ifndef _ASSERT_H
#define _ASSERT_H

#include <stdio.h>

/*
 * assert - 断言宏
 *
 * 如果表达式为假，打印错误信息并停止程序
 * 如果定义了 NDEBUG，assert 被禁用
 */

#ifdef NDEBUG
    #define assert(expr) ((void)0)
#else
    #define assert(expr) \
        do { \
            if (!(expr)) { \
                printf("\n*** ASSERTION FAILED ***\n"); \
                printf("File: %s\n", __FILE__); \
                printf("Line: %d\n", __LINE__); \
                printf("Expression: %s\n", #expr); \
                printf("*** TEST FAILED ***\n\n"); \
                while(1); /* 停止程序 */ \
            } \
        } while(0)
#endif

/*
 * static_assert - 编译时断言（C11）
 *
 * 在编译时检查条件，如果为假则编译失败
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define static_assert _Static_assert
#endif

#endif /* _ASSERT_H */
