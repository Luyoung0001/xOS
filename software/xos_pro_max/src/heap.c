/*------------------------------------------------------------------------------
 * xOS Heap Memory Manager Implementation
 *
 * Simple first-fit allocator with free list
 *----------------------------------------------------------------------------*/

#include <heap.h>
#include <stdio.h>
#include <string.h>

/*============================================================================
 * Memory Block Header
 *============================================================================*/
typedef struct block_header {
    size_t size;                    /* Size of this block (excluding header) */
    struct block_header* next;      /* Next block in free list */
    int is_free;                    /* 1 = free, 0 = allocated */
} block_header_t;

#define BLOCK_HEADER_SIZE sizeof(block_header_t)
#define ALIGN_SIZE 8
// why have to align size?
// because some architectures require memory to be aligned to certain boundaries
#define ALIGN(size) (((size) + (ALIGN_SIZE - 1)) & ~(ALIGN_SIZE - 1))

/*============================================================================
 * Global Variables
 *============================================================================*/
static block_header_t* free_list = NULL;
static uint32_t heap_total = 0;
static uint32_t heap_used = 0;
static int heap_initialized = 0;

/*============================================================================
 * Helper Functions
 *============================================================================*/

/* Merge adjacent free blocks */
static void merge_free_blocks(void) {
    block_header_t* current = free_list;

    while (current && current->next) {
        /* Check if current and next are adjacent */
        char* current_end = (char*)current + BLOCK_HEADER_SIZE + current->size;
        char* next_start = (char*)current->next;

        if (current->is_free && current->next->is_free && current_end == next_start) {
            /* Merge blocks */
            current->size += BLOCK_HEADER_SIZE + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
}

/*============================================================================
 * API Implementation
 *============================================================================*/

void heap_init(void) {
    if (heap_initialized) {
        return;
    }

    /* Initialize free list with entire heap */
    free_list = (block_header_t*)HEAP_BASE;
    // size means the size excluding header
    free_list->size = HEAP_SIZE - BLOCK_HEADER_SIZE;
    // at beginning, there is only one big free block
    free_list->next = NULL;
    free_list->is_free = 1;

    // Initialize stats
    heap_total = HEAP_SIZE;
    // i think used size should be the size of headers of allocated blocks
    heap_used = BLOCK_HEADER_SIZE;  // initial header
    heap_initialized = 1;

    printf("[HEAP] Initialized: base=0x%08x, size=%uMB\n",
           HEAP_BASE, HEAP_SIZE / (1024 * 1024));
}

void* malloc(size_t size) {
    if (!heap_initialized) {
        heap_init();
    }

    if (size == 0) {
        return NULL;
    }

    /* Align size */
    size = ALIGN(size);

    /* Search free list for suitable block (first-fit) */
    block_header_t* current = free_list;

    while (current) {
        if (current->is_free && current->size >= size) {
            /* Found suitable block */
            if (current->size >= size + BLOCK_HEADER_SIZE + ALIGN_SIZE) {
                /* Split block */
                block_header_t* new_block = (block_header_t*)((char*)current + BLOCK_HEADER_SIZE + size);
                // new block size is remaining size,also exclude header size
                new_block->size = current->size - size - BLOCK_HEADER_SIZE;

                // new block join the list
                new_block->next = current->next;
                new_block->is_free = 1;

                current->size = size;
                current->next = new_block;
            }

            current->is_free = 0;
            // used size equals allocated size + header size
            heap_used += current->size + BLOCK_HEADER_SIZE;
            // when align size, the returned pointer is always aligned
            // and only return the memory after header, we do not return header to user
            return (void*)((char*)current + BLOCK_HEADER_SIZE);
        }
        current = current->next;
    }

    /* No suitable block found */
    return NULL;
}

void free(void* ptr) {
    if (!ptr || !heap_initialized) {
        return;
    }

    /* Get block header */
    block_header_t* block = (block_header_t*)((char*)ptr - BLOCK_HEADER_SIZE);

    /* Mark as free */
    block->is_free = 1;
    heap_used -= block->size + BLOCK_HEADER_SIZE;

    /* Merge adjacent free blocks */
    merge_free_blocks();
}

void* calloc(size_t nmemb, size_t size) {
    size_t total_size = nmemb * size;
    void* ptr = malloc(total_size);

    if (ptr) {
        memset(ptr, 0, total_size);
    }

    return ptr;
}

void heap_stats(uint32_t* total, uint32_t* used, uint32_t* free_size) {
    if (total) *total = heap_total;
    if (used) *used = heap_used;
    if (free_size) *free_size = heap_total - heap_used;
}
