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
#include <math.h>
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



//constant
#define WSIZE 8
#define DSIZE 16
#define ALIGNMENT 8

#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

//metadata
#define PACK(size, alloc_bit) ((size) | (alloc_bit)) // size와 alloc bit를 하나의 값으로 합침
#define GET(bp) (*(size_t*)(bp)) // 주소에서 metadata를 읽음
#define PUT(bp, value) (*(size_t*)(bp) = (value)) // 주소에 metadata를 씀

//metadata translator
#define GET_SIZE(value) ((value) & ~0x7) // metadata에서 block size만 추출
#define GET_ALLOC(value) ((value) & 0x1) // metadata에서 allocation bit만 추출

//block
#define HDRP(bp) (size_t*)((char*)bp - 8) // payload 주소 → header 주소
#define FTRP(bp) (size_t*)((char*)bp + GET_SIZE(HDRP(bp)) - 8) // payload 주소 → footer 주소

typedef struct free_block {
    char* bp;
    struct free_block* prev;
    struct free_block* next;
} free_block;

typedef struct head_block {
    free_block* entry;
} head_block;

head_block head_ary[10];

void init(head_block head_ary[10])
{
    for (int i=0; i<10; i++)
        head_ary[i].entry = NULL;
}

static char* heap_start;
/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    heap_start = mem_sbrk(DSIZE + WSIZE);

    if (heap_start == (void*)-1)
        return -1;

    PUT(heap_start, PACK(WSIZE, 1));
    PUT(heap_start + WSIZE, PACK(WSIZE, 1));
    PUT(heap_start + DSIZE, PACK(0, 1));
    
    return 0;
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */

int get_class_index(size_t block_size)
{
    if (block_size >= 8192) return 9;
    else if (block_size >= 4096) return 8;
    else if (block_size >= 2048) return 7;
    else if (block_size >= 1024) return 6;
    else if (block_size >= 512) return 5;
    else if (block_size >= 256) return 4;
    else if (block_size >= 128) return 3;
    else if (block_size >= 64) return 2;
    else if (block_size >= 32) return 1;
    else return 0;
}


void *mm_malloc(size_t size)
{
    size_t adjusted_block_size = ALIGN(size + DSIZE);
    int index = get_class_index(adjusted_block_size);
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{

}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{

}

