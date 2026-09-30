/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

static char* heap_start;
/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    heap_start = mem_sbrk(24);
    if (heap_start == (void*)-1)
        return -1;
    
    *(size_t*)heap_start = 8|1;
    *(size_t*)((char*)heap_start + 8) = 8|1;
    *(size_t*)((char*)heap_start + 16) = 0|1;

    return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t block_size = ALIGN(size + 8 + 8);
    void* p = heap_start;
    size_t metadata = *(size_t*)((char*)p + 16);
    p += 16;

    while ((metadata & ~1) != 0 && ((metadata & ~1) < block_size || (metadata & 1) == 1))
    {
        p += (metadata & ~1);
        metadata = *(size_t*)((char*)p);
    }

    if ((metadata & ~1) == 0)
    {
        void* q = mem_sbrk(block_size);
        if (q == (void*)-1)
        {
            return NULL;
        }
        else
        {
            *(size_t *)((char*)p) = block_size|1;
            *(size_t *)((char*)p + block_size - 8) = block_size|1;
            *(size_t *)((char*)p + block_size) = 0|1; 
            return (void *)((char *)p+8);
        }
    }
    else
    {
        metadata = *(size_t*)p|1;
        *(size_t*)((char*)p) = metadata;
        *(size_t*)((char*)p + (metadata&~1) - 8) = metadata;
        return (char*)(p+8);
    }
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    if (ptr == NULL)
        return;

    size_t current_size = *(size_t *)((char*)ptr - 8) & ~1;
    size_t prev_alloc = *(size_t *)((char*)ptr - 16) & 1;
    size_t prev_size = *(size_t *)((char*)ptr - 16) & ~1;
    size_t next_alloc = *(size_t *)((char*)ptr -8 + current_size) & 1;
    size_t next_size = *(size_t *)((char*)ptr -8 + current_size) & ~1;

    if (prev_alloc == 1 && next_alloc == 1)
    {
        *(size_t *)((char*)ptr -8) = *(size_t *)((char*)ptr -8) & ~1;
        *(size_t *)((char*)ptr -8 + current_size - 8) = *(size_t *)((char*)ptr -8 + current_size - 8) & ~1;
    }
    else if (prev_alloc == 1 && next_alloc == 0)
    {
        *(size_t *)((char*)ptr - 8) = current_size + next_size;
        *(size_t *)((char*)ptr - 8 + current_size + next_size - 8) = current_size + next_size;
    }
    else if (prev_alloc == 0 && next_alloc == 1)
    {
        *(size_t *)((char*)ptr - 8 - prev_size) = prev_size + current_size;
        *(size_t *)((char*)ptr - 8 + current_size - 8) = prev_size + current_size;
    }
    else
    {
        *(size_t *)((char*)ptr - 8 - prev_size) = prev_size + current_size + next_size;
        *(size_t *)((char*)ptr - 8 + current_size + next_size - 8) = prev_size + current_size + next_size;
    }
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    if (ptr == NULL)
        return mm_malloc(size);
    
    if (size == 0)
    {
        mm_free(ptr);
        return NULL;
    }

    size_t current_size = *(size_t *)((char*)ptr - 8) & ~1;
    size_t new_size = ALIGN(size + 16);

    if (new_size <= current_size)
        return ptr;

    if (new_size > current_size)
    {
        void *dest = mm_malloc(size);

        if (dest == NULL)
            return NULL;
        
        memcpy(dest, ptr, current_size - 16);
        mm_free(ptr);
        return dest;
    }
    return NULL;
}