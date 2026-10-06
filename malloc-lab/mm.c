/*
 * Case 1: External Free List
 * Placement: Best Fit
 * Splitting
 * Minimum Block Size: 32B
 * Immediate Coalescing
 * Heap Extension
 * In-place realloc
 *
 * IMPORTANT:
 * - Simulated heap blocks live in memlib's heap.
 * - Free-list nodes are real libc malloc'd objects, outside the
 *   simulated heap used by mdriver for utilization measurement.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    "ateam",
    "Harry Bovik",
    "bovik@cs.cmu.edu",
    "",
    ""
};

#define WSIZE       8
#define DSIZE       16
#define ALIGNMENT   8
#define MIN_BLOCK_SIZE 32

#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~((size_t)(ALIGNMENT - 1)))
#define PACK(size, alloc) ((size) | (size_t)(alloc))
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))
#define GET_SIZE(val) ((val) & ~((size_t)0x7))
#define GET_ALLOC(val) ((val) & (size_t)0x1)

#define HDRP(bp) ((char *)(bp) - WSIZE)
#define FTRP(bp) ((char *)(bp) + GET_SIZE(GET(HDRP(bp))) - DSIZE)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(GET(HDRP(bp))))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(GET((char *)(bp) - DSIZE)))

typedef struct free_block {
    char *bp;
    struct free_block *prev;
    struct free_block *next;
} free_block;

static free_block *free_list_head = NULL;

static size_t adjust_block_size(size_t size)
{
    size_t asize;

    if (size > (size_t)-1 - DSIZE)
        return 0;

    asize = ALIGN(size + DSIZE);
    if (asize < MIN_BLOCK_SIZE)
        asize = MIN_BLOCK_SIZE;

    return asize;
}

static void write_block(char *bp, size_t size, int alloc)
{
    PUT(HDRP(bp), PACK(size, alloc));
    PUT(FTRP(bp), PACK(size, alloc));
}

/* Find the external node corresponding to a simulated-heap block. */
static free_block *find_node(char *bp)
{
    free_block *cur = free_list_head;

    while (cur != NULL) {
        if (cur->bp == bp)
            return cur;
        cur = cur->next;
    }

    return NULL;
}

static int insert_node(char *bp)
{
    free_block *node = (free_block *)malloc(sizeof(free_block));

    if (node == NULL)
        return -1;

    node->bp = bp;
    node->prev = NULL;
    node->next = free_list_head;

    if (free_list_head != NULL)
        free_list_head->prev = node;

    free_list_head = node;
    return 0;
}

static void remove_node(free_block *node)
{
    if (node == NULL)
        return;

    if (node->prev != NULL)
        node->prev->next = node->next;
    else
        free_list_head = node->next;

    if (node->next != NULL)
        node->next->prev = node->prev;

    free(node);
}

static void clear_free_list(void)
{
    free_block *cur = free_list_head;

    while (cur != NULL) {
        free_block *next = cur->next;
        free(cur);
        cur = next;
    }

    free_list_head = NULL;
}

static free_block *find_best_fit(size_t asize)
{
    free_block *cur = free_list_head;
    free_block *best = NULL;
    size_t best_size = (size_t)-1;

    while (cur != NULL) {
        size_t size = GET_SIZE(GET(HDRP(cur->bp)));

        if (size >= asize && size < best_size) {
            best = cur;
            best_size = size;

            if (size == asize)
                break;
        }

        cur = cur->next;
    }

    return best;
}

static char *coalesce(char *bp)
{
    size_t size = GET_SIZE(GET(HDRP(bp)));
    int prev_free = !GET_ALLOC(GET(HDRP(PREV_BLKP(bp))));
    int next_free = !GET_ALLOC(GET(HDRP(NEXT_BLKP(bp))));

    if (prev_free && next_free) {
        char *prev = PREV_BLKP(bp);
        char *next = NEXT_BLKP(bp);
        free_block *prev_node = find_node(prev);
        free_block *next_node = find_node(next);

        size += GET_SIZE(GET(HDRP(prev))) + GET_SIZE(GET(HDRP(next)));

        remove_node(prev_node);
        remove_node(next_node);

        bp = prev;
        write_block(bp, size, 0);
    }
    else if (prev_free) {
        char *prev = PREV_BLKP(bp);
        free_block *prev_node = find_node(prev);

        size += GET_SIZE(GET(HDRP(prev)));
        remove_node(prev_node);

        bp = prev;
        write_block(bp, size, 0);
    }
    else if (next_free) {
        char *next = NEXT_BLKP(bp);
        free_block *next_node = find_node(next);

        size += GET_SIZE(GET(HDRP(next)));
        remove_node(next_node);

        write_block(bp, size, 0);
    }

    /* Exactly one external node for the resulting free block. */
    if (insert_node(bp) < 0)
        return NULL;

    return bp;
}

static char *extend_heap(size_t asize)
{
    char *bp;

    if (asize > INT_MAX)
        return NULL;

    bp = (char *)mem_sbrk((int)asize);
    if (bp == (char *)-1)
        return NULL;

    /* bp points at the old epilogue; its header is HDRP(bp). */
    write_block(bp, asize, 0);
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    return bp;
}

int mm_init(void)
{
    char *heap_listp;

    /* Free external nodes left by a previous mdriver trial. */
    clear_free_list();

    heap_listp = (char *)mem_sbrk(4 * WSIZE);
    if (heap_listp == (char *)-1)
        return -1;

    PUT(heap_listp, 0);
    PUT(heap_listp + WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 2 * WSIZE, PACK(DSIZE, 1));
    PUT(heap_listp + 3 * WSIZE, PACK(0, 1));

    return 0;
}

void *mm_malloc(size_t size)
{
    size_t asize;
    free_block *node;
    char *bp;

    if (size == 0)
        return NULL;

    asize = adjust_block_size(size);
    if (asize == 0)
        return NULL;

    node = find_best_fit(asize);
    if (node != NULL) {
        size_t csize = GET_SIZE(GET(HDRP(node->bp)));
        bp = node->bp;

        remove_node(node);

        if (csize - asize >= MIN_BLOCK_SIZE) {
            char *remainder = bp + asize;

            write_block(bp, asize, 1);
            write_block(remainder, csize - asize, 0);

            if (insert_node(remainder) < 0) {
                /* Roll back to a single allocated block. */
                write_block(bp, csize, 1);
            }
        }
        else {
            write_block(bp, csize, 1);
        }

        return bp;
    }

    /* No fit: extend the simulated heap and allocate immediately. */
    bp = extend_heap(asize);
    if (bp == NULL)
        return NULL;

    write_block(bp, asize, 1);
    return bp;
}

void mm_free(void *ptr)
{
    size_t size;

    if (ptr == NULL)
        return;

    size = GET_SIZE(GET(HDRP(ptr)));
    write_block((char *)ptr, size, 0);

    if (coalesce((char *)ptr) == NULL) {
        /* libc malloc failure for a bookkeeping node is unrecoverable
           without changing the allocator's free-list representation. */
        abort();
    }
}

void *mm_realloc(void *ptr, size_t size)
{
    size_t asize;
    size_t old_size;

    if (ptr == NULL)
        return mm_malloc(size);

    if (size == 0) {
        mm_free(ptr);
        return NULL;
    }

    asize = adjust_block_size(size);
    if (asize == 0)
        return NULL;

    old_size = GET_SIZE(GET(HDRP(ptr)));

    /* Shrink in place. */
    if (asize <= old_size) {
        if (old_size - asize >= MIN_BLOCK_SIZE) {
            char *remainder = (char *)ptr + asize;

            write_block((char *)ptr, asize, 1);
            write_block(remainder, old_size - asize, 0);

            if (coalesce(remainder) == NULL)
                abort();
        }

        return ptr;
    }

    /* Grow in place using the next free block. */
    {
        char *next = NEXT_BLKP((char *)ptr);
        size_t next_alloc = GET_ALLOC(GET(HDRP(next)));

        if (!next_alloc) {
            size_t next_size = GET_SIZE(GET(HDRP(next)));
            size_t combined = old_size + next_size;
            free_block *next_node = find_node(next);

            if (combined >= asize) {
                remove_node(next_node);

                if (combined - asize >= MIN_BLOCK_SIZE) {
                    char *remainder = (char *)ptr + asize;

                    write_block((char *)ptr, asize, 1);
                    write_block(remainder, combined - asize, 0);

                    if (insert_node(remainder) < 0)
                        abort();
                }
                else {
                    write_block((char *)ptr, combined, 1);
                }

                return ptr;
            }
        }
    }

    /* Grow at the end of the heap in place. */
    {
        char *next = NEXT_BLKP((char *)ptr);
        size_t next_size = GET_SIZE(GET(HDRP(next)));

        if (next_size == 0 && GET_ALLOC(GET(HDRP(next)))) {
            size_t extra = asize - old_size;

            if (extra <= INT_MAX) {
                if (mem_sbrk((int)extra) != (void *)-1) {
                    write_block((char *)ptr, asize, 1);
                    PUT(HDRP(NEXT_BLKP((char *)ptr)), PACK(0, 1));
                    return ptr;
                }
            }
        }
    }

    /* Fallback: allocate, copy, free. */
    {
        void *new_ptr = mm_malloc(size);
        size_t copy_size;

        if (new_ptr == NULL)
            return NULL;

        copy_size = old_size - DSIZE;
        if (copy_size > size)
            copy_size = size;

        memcpy(new_ptr, ptr, copy_size);
        mm_free(ptr);

        return new_ptr;
    }
}
