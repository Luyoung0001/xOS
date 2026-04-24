/*------------------------------------------------------------------------------
 * xOS Heap Memory Manager
 *
 * Simple heap allocator for dynamic memory allocation
 *----------------------------------------------------------------------------*/

#ifndef __HEAP_H__
#define __HEAP_H__

#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * Memory Layout Configuration
 *============================================================================*/
// 堆起止地址改为使用链接脚本导出的符号，保证 QEMU/硬件版一致
extern char __heap_start[];
extern char __heap_end[];
#define HEAP_BASE       ((uintptr_t)__heap_start)
#define HEAP_SIZE       ((uintptr_t)__heap_end - (uintptr_t)__heap_start)
#define HEAP_END        ((uintptr_t)__heap_end - 1)

// Task output buffers region
#define OUTPUT_BUF_BASE 0x10000000  // 256MB offset
#define OUTPUT_BUF_SIZE (8 * 1024 * 1024)  // 8MB (8 tasks × 1MB)
#define OUTPUT_BUF_END  (OUTPUT_BUF_BASE + OUTPUT_BUF_SIZE - 1)

// Extended heap region (if needed)
#define HEAP_EXT_BASE   0x10800000  // After output buffers
#define HEAP_EXT_SIZE   (120 * 1024 * 1024)  // 120MB
#define HEAP_EXT_END    (HEAP_EXT_BASE + HEAP_EXT_SIZE - 1)

// Stack region
#define STACK_TOP       0x1EFFFFFF
#define STACK_BOTTOM    0x1D000000  // 32MB

// Framebuffer region
#define FRAMEBUF_BASE   0x1F000000
#define FRAMEBUF_SIZE   0x01000000  // 16MB (双缓冲 1080P)


/*============================================================================
 * Heap Manager API
 *============================================================================*/

/**
 * Initialize heap memory manager
 * Must be called before any malloc/free operations
 */
void heap_init(void);

/**
 * Allocate memory from heap
 * @param size: Number of bytes to allocate
 * @return: Pointer to allocated memory, or NULL if failed
 */
void* malloc(size_t size);

/**
 * Free previously allocated memory
 * @param ptr: Pointer to memory to free
 */
void free(void* ptr);

/**
 * Allocate and zero-initialize memory
 * @param nmemb: Number of elements
 * @param size: Size of each element
 * @return: Pointer to allocated memory, or NULL if failed
 */
void* calloc(size_t nmemb, size_t size);

/**
 * Get heap statistics
 * @param total: Total heap size (output)
 * @param used: Used heap size (output)
 * @param free: Free heap size (output)
 */
void heap_stats(uint32_t* total, uint32_t* used, uint32_t* free);

#endif /* __HEAP_H__ */
