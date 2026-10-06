/*
 * mm.c
 *
 * External Free List + Best Fit + Splitting
 * + Immediate Coalescing + In-place realloc
 *
 * Heap layout of each real block:
 *
 *   [ external node (24B) ][ header (8B) ][ payload ... ][ footer (8B) ]
 *
 * The 24-byte node is physically reserved for every block.
 * It is linked into the external free list only while the block is free.
 *
 * Block size stored in header/footer includes:
 *
 *   header + payload + footer
 *
 * and does NOT include the 24-byte external node.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>

#include "mm.h"
#include "memlib.h"


/*********************************************************
 * Team information
 *********************************************************/

team_t team = {
    "ateam",
    "Harry Bovik",
    "bovik@cs.cmu.edu",
    "",
    ""
};


/*********************************************************
 * Constants
 *********************************************************/

#define WSIZE 8
#define DSIZE 16
#define ALIGNMENT 8

/*
 * Minimum block size:
 *
 * Header  :  8B
 * Payload : 16B
 * Footer  :  8B
 *
 * Total = 32B
 */
#define MIN_BLOCK_SIZE 32

/*
 * External free-list node:
 *
 * bp   :  8B
 * prev :  8B
 * next :  8B
 *
 * Total = 24B
 */
#define NODE_SIZE 24


/*********************************************************
 * Metadata helpers
 *********************************************************/

#define ALIGN(size) \
    (((size) + (ALIGNMENT - 1)) & ~((size_t)(ALIGNMENT - 1)))

#define PACK(size, alloc_bit) \
    ((size) | (size_t)(alloc_bit))

#define GET(p) \
    (*(size_t *)(p))

#define PUT(p, value) \
    (*(size_t *)(p) = (value))

#define GET_SIZE(value) \
    ((value) & ~((size_t)0x7))

#define GET_ALLOC(value) \
    ((value) & (size_t)0x1)


/*********************************************************
 * Block helpers
 *
 * bp = first byte of payload
 *********************************************************/

#define HDRP(bp) \
    ((char *)(bp) - WSIZE)

#define FTRP(bp) \
    ((char *)(bp) + GET_SIZE(GET(HDRP(bp))) - DSIZE)

/*
 * The node belonging to bp is immediately before its header.
 *
 * [ node 24B ][ header 8B ][ payload ... ][ footer 8B ]
 */
#define NODEP(bp) \
    ((free_block *)((char *)(bp) - WSIZE - NODE_SIZE))


/*********************************************************
 * External free-list node
 *********************************************************/

typedef struct free_block {
    char *bp;

    struct free_block *prev;
    struct free_block *next;

} free_block;


/*********************************************************
 * Free-list head
 *********************************************************/

typedef struct head_block {
    free_block *entry;
} head_block;


/*********************************************************
 * Global state
 *********************************************************/

/*
 * Payload pointer of the prologue block.
 */
static char *heap_start;

/*
 * Payload pointer of the last real heap block.
 */
static char *last_block;

/*
 * Free-list head.
 */
static head_block free_list;


/*********************************************************
 * Adjust requested payload size
 *
 * requested payload
 *        ↓
 * payload + header + footer
 *        ↓
 * alignment
 *        ↓
 * minimum 32B
 *********************************************************/

static size_t adjusted_size(size_t size)
{
    size_t asize;

    /*
     * Prevent overflow in size + DSIZE.
     */
    if (size > (size_t)-1 - DSIZE)
        return 0;

    asize = ALIGN(size + DSIZE);

    if (asize < MIN_BLOCK_SIZE)
        asize = MIN_BLOCK_SIZE;

    return asize;
}


/*********************************************************
 * Free-list insertion
 *
 * Insert at head.
 *********************************************************/

static void list_insert(free_block *node)
{
    node->prev = NULL;
    node->next = free_list.entry;

    if (free_list.entry != NULL)
        free_list.entry->prev = node;

    free_list.entry = node;
}


/*********************************************************
 * Free-list removal
 *********************************************************/

static void list_remove(free_block *node)
{
    if (node == NULL)
        return;

    if (node->prev != NULL) {
        node->prev->next = node->next;
    }
    else if (free_list.entry == node) {
        free_list.entry = node->next;
    }

    if (node->next != NULL)
        node->next->prev = node->prev;

    node->prev = NULL;
    node->next = NULL;
}


/*********************************************************
 * Write block metadata
 *********************************************************/

static void write_block(char *bp, size_t size, int alloc)
{
    PUT(HDRP(bp), PACK(size, alloc));
    PUT(FTRP(bp), PACK(size, alloc));
}


/*********************************************************
 * Previous physical block
 *
 * Layout:
 *
 * [ prev node ][ prev header ][ prev payload ][ prev footer ]
 *                                                   |
 *                                                   ↓
 *                                              current node
 *                                              current header
 *
 * The footer immediately before current node identifies
 * the previous block.
 *********************************************************/

static char *prev_bp(char *bp)
{
    char *prev_footer;
    size_t prev_size;

    prev_footer =
        (char *)HDRP(bp) - NODE_SIZE - WSIZE;

    prev_size =
        GET_SIZE(GET(prev_footer));

    return prev_footer - prev_size + DSIZE;
}


/*********************************************************
 * Next physical block
 *********************************************************/

static char *next_bp(char *bp)
{
    /*
     * [ current footer ]
     * [ next node      ]
     * [ next header    ]
     * [ next payload   ]
     */
    return
        (char *)FTRP(bp)
        + WSIZE
        + NODE_SIZE
        + WSIZE;
}


/*********************************************************
 * Previous allocation status
 *********************************************************/

static int prev_alloc(char *bp)
{
    char *prev_footer;

    prev_footer =
        (char *)HDRP(bp) - NODE_SIZE - WSIZE;

    return (int)GET_ALLOC(GET(prev_footer));
}


/*********************************************************
 * Next allocation status
 *********************************************************/

static int next_alloc(char *bp)
{
    /*
     * last_block has the epilogue immediately after it,
     * so it has no next real block.
     */
    if (bp == last_block)
        return 1;

    return (int)GET_ALLOC(GET(HDRP(next_bp(bp))));
}


/*********************************************************
 * Best Fit search
 *
 * Search the entire free list.
 *
 * Choose the smallest free block whose size is >= asize.
 *********************************************************/

static free_block *best_fit(size_t asize)
{
    free_block *current;
    free_block *best;

    size_t best_size;

    current = free_list.entry;
    best = NULL;
    best_size = (size_t)-1;

    while (current != NULL) {

        char *bp;
        size_t current_size;

        bp = current->bp;

        current_size =
            GET_SIZE(GET(HDRP(bp)));

        if (current_size >= asize &&
            current_size < best_size) {

            best = current;
            best_size = current_size;

            /*
             * Exact fit.
             */
            if (current_size == asize)
                break;
        }

        current = current->next;
    }

    return best;
}


/*********************************************************
 * Immediate coalescing
 *
 * Four logical cases:
 *
 * 1. prev alloc / next alloc
 * 2. prev alloc / next free
 * 3. prev free  / next alloc
 * 4. prev free  / next free
 *********************************************************/

static char *coalesce(char *bp)
{
    char *original_bp;
    char *prev;
    char *next;

    size_t size;

    int prev_is_alloc;
    int next_is_alloc;

    original_bp = bp;

    prev = NULL;
    next = NULL;

    size =
        GET_SIZE(GET(HDRP(bp)));

    prev_is_alloc =
        prev_alloc(bp);

    next_is_alloc =
        next_alloc(bp);

    /*
     * Important:
     *
     * Calculate adjacent block addresses BEFORE changing bp
     * or its header.
     */
    if (!prev_is_alloc)
        prev = prev_bp(original_bp);

    if (!next_is_alloc)
        next = next_bp(original_bp);


    /*
     * Merge with previous free block.
     */
    if (!prev_is_alloc) {

        size_t prev_size;
        int current_was_last;

        prev_size =
            GET_SIZE(GET(HDRP(prev)));

        current_was_last =
            (original_bp == last_block);

        /*
         * Previous block is already in the free list.
         */
        list_remove(NODEP(prev));

        bp = prev;

        /*
         * Previous block + node + current block
         */
        size +=
            NODE_SIZE + prev_size;

        if (current_was_last)
            last_block = bp;
    }


    /*
     * Merge with next free block.
     */
    if (!next_is_alloc) {

        size_t next_size;

        next_size =
            GET_SIZE(GET(HDRP(next)));

        /*
         * Next block is already in the free list.
         */
        list_remove(NODEP(next));

        /*
         * Current/merged block + node + next block
         */
        size +=
            NODE_SIZE + next_size;

        if (next == last_block)
            last_block = bp;
    }


    /*
     * Write the final merged block metadata.
     */
    write_block(
        bp,
        size,
        0
    );


    /*
     * Exactly one node represents the final free block.
     */
    list_insert(NODEP(bp));

    return bp;
}


/*********************************************************
 * Heap extension
 *
 * Old epilogue is replaced by:
 *
 * [ node ][ header ][ payload ][ footer ][ epilogue ]
 *********************************************************/

static char *extend_heap(size_t asize)
{
    size_t total;

    char *node_mem;
    char *bp;

    free_block *node;

    total =
        NODE_SIZE + asize;

    /*
     * mem_sbrk takes int.
     */
    if (total > (size_t)INT_MAX)
        return NULL;

    node_mem =
        (char *)mem_sbrk((int)total);

    if (node_mem == (void *)-1)
        return NULL;


    /*
     * The new node belongs to this new block.
     */
    node =
        (free_block *)node_mem;

    bp =
        node_mem
        + NODE_SIZE
        + WSIZE;


    node->bp = bp;
    node->prev = NULL;
    node->next = NULL;


    /*
     * New block is allocated directly.
     */
    write_block(
        bp,
        asize,
        1
    );


    /*
     * New epilogue.
     */
    PUT(
        (char *)FTRP(bp) + WSIZE,
        PACK(0, 1)
    );


    last_block = bp;

    return bp;
}


/*********************************************************
 * mm_init
 *********************************************************/

int mm_init(void)
{
    char *base;

    free_block *prologue_node;

    char *prologue_bp;


    /*
     * Prologue also has a permanent node so that
     * prev_bp()/prev_alloc() work uniformly for the
     * first real block.
     *
     * [ node 24 ]
     * [ header 8 ]
     * [ footer 8 ]
     * [ epilogue 8 ]
     */
    base =
        (char *)mem_sbrk(
            NODE_SIZE + DSIZE + WSIZE
        );

    if (base == (void *)-1)
        return -1;


    prologue_node =
        (free_block *)base;

    prologue_bp =
        base
        + NODE_SIZE
        + WSIZE;


    /*
     * Prologue node is not part of free list.
     */
    prologue_node->bp =
        prologue_bp;

    prologue_node->prev = NULL;
    prologue_node->next = NULL;


    /*
     * Prologue block.
     */
    PUT(
        HDRP(prologue_bp),
        PACK(DSIZE, 1)
    );

    PUT(
        FTRP(prologue_bp),
        PACK(DSIZE, 1)
    );


    /*
     * Epilogue.
     */
    PUT(
        (char *)FTRP(prologue_bp) + WSIZE,
        PACK(0, 1)
    );


    heap_start =
        prologue_bp;

    last_block =
        NULL;

    free_list.entry =
        NULL;

    return 0;
}


/*********************************************************
 * mm_malloc
 *********************************************************/

void *mm_malloc(size_t size)
{
    size_t asize;

    free_block *node;


    /*
     * malloc(0)
     */
    if (size == 0)
        return NULL;


    /*
     * Requested payload -> actual block size.
     */
    asize =
        adjusted_size(size);

    if (asize == 0)
        return NULL;


    /*
     * Step 1:
     * Best Fit search.
     */
    node =
        best_fit(asize);


    /*
     * Step 2:
     * Existing free block found.
     */
    if (node != NULL) {

        char *bp;
        size_t block_size;


        bp =
            node->bp;

        block_size =
            GET_SIZE(GET(HDRP(bp)));


        /*
         * Remove from free list first.
         */
        list_remove(node);


        /*
         * Splitting condition:
         *
         * remainder must contain:
         *
         *   24B external node
         *   + 32B minimum block
         */
        if (block_size
            >= asize
            + NODE_SIZE
            + MIN_BLOCK_SIZE) {

            char *new_bp;
            free_block *new_node;


            /*
             * First part becomes allocated.
             *
             * Its existing node remains its node.
             */
            write_block(
                bp,
                asize,
                1
            );


            /*
             * Remainder begins after:
             *
             * allocated block
             * + 24B node
             */
            new_bp =
                bp
                + asize
                + NODE_SIZE;


            /*
             * new_bp's node starts immediately before
             * its header.
             */
            new_node =
                NODEP(new_bp);


            new_node->bp =
                new_bp;

            new_node->prev =
                NULL;

            new_node->next =
                NULL;


            /*
             * Remaining free block.
             */
            write_block(
                new_bp,
                block_size
                    - asize
                    - NODE_SIZE,
                0
            );


            /*
             * If the original block was the last block,
             * remainder becomes the new last block.
             */
            if (bp == last_block)
                last_block =
                    new_bp;


            /*
             * Add remainder to free list.
             */
            list_insert(
                new_node
            );
        }
        else {

            /*
             * No valid remainder.
             * Consume the whole free block.
             */
            write_block(
                bp,
                block_size,
                1
            );
        }


        return bp;
    }


    /*
     * Step 3:
     * No suitable free block.
     *
     * Extend heap and allocate.
     */
    return extend_heap(asize);
}


/*********************************************************
 * mm_free
 *********************************************************/

void mm_free(void *ptr)
{
    size_t size;

    free_block *node;


    /*
     * free(NULL)
     */
    if (ptr == NULL)
        return;


    /*
     * Read current block size.
     */
    size =
        GET_SIZE(GET(HDRP(ptr)));


    /*
     * allocated -> free
     */
    write_block(
        ptr,
        size,
        0
    );


    /*
     * This block's external node.
     */
    node =
        NODEP(ptr);


    node->bp =
        ptr;

    node->prev =
        NULL;

    node->next =
        NULL;


    /*
     * Immediate coalescing.
     *
     * coalesce() inserts the final block into
     * the free list.
     */
    coalesce(ptr);
}


/*********************************************************
 * mm_realloc
 *********************************************************/

void *mm_realloc(void *ptr, size_t size)
{
    size_t asize;
    size_t old_size;


    /*
     * realloc(NULL, size)
     * == malloc(size)
     */
    if (ptr == NULL)
        return mm_malloc(size);


    /*
     * realloc(ptr, 0)
     * == free(ptr)
     */
    if (size == 0) {

        mm_free(ptr);

        return NULL;
    }


    /*
     * New block size.
     */
    asize =
        adjusted_size(size);

    if (asize == 0)
        return NULL;


    /*
     * Old block size.
     */
    old_size =
        GET_SIZE(GET(HDRP(ptr)));


    /*****************************************************
     * Case 1:
     *
     * shrink / same-size
     *
     * Keep pointer unchanged.
     *****************************************************/

    if (asize <= old_size) {

        /*
         * Split only if the remainder can form
         * a node + minimum block.
         */
        if (old_size
            >= asize
            + NODE_SIZE
            + MIN_BLOCK_SIZE) {

            char *new_bp;
            free_block *node;


            /*
             * Shrink current allocated block.
             */
            write_block(
                ptr,
                asize,
                1
            );


            /*
             * Create node for remainder.
             */
            new_bp =
                (char *)ptr
                + asize
                + NODE_SIZE;


            node =
                NODEP(new_bp);


            node->bp =
                new_bp;

            node->prev =
                NULL;

            node->next =
                NULL;


            /*
             * Remainder becomes free block.
             */
            write_block(
                new_bp,
                old_size
                    - asize
                    - NODE_SIZE,
                0
            );


            /*
             * If original block was last,
             * remainder becomes last.
             */
            if (ptr == last_block)
                last_block =
                    new_bp;


            /*
             * Immediate coalescing with a possible
             * free next block.
             */
            coalesce(new_bp);
        }


        return ptr;
    }


    /*****************************************************
     * Case 2:
     *
     * Grow in place using the next free block.
     *****************************************************/

    if (!next_alloc(ptr)) {

        char *next;

        size_t next_size;
        size_t combined_size;

        int next_was_last;


        /*
         * Save next block information before modifying it.
         */
        next =
            next_bp(ptr);

        next_size =
            GET_SIZE(GET(HDRP(next)));

        combined_size =
            old_size
            + NODE_SIZE
            + next_size;

        next_was_last =
            (next == last_block);


        /*
         * Enough space for in-place growth.
         */
        if (combined_size >= asize) {

            /*
             * Next block is consumed.
             */
            list_remove(
                NODEP(next)
            );


            /*
             * Start with combined allocated block.
             */
            write_block(
                ptr,
                combined_size,
                1
            );


            /*
             * Split the combined space if possible.
             */
            if (combined_size
                >= asize
                + NODE_SIZE
                + MIN_BLOCK_SIZE) {

                char *new_bp;
                free_block *node;


                /*
                 * Resize current allocated block.
                 */
                write_block(
                    ptr,
                    asize,
                    1
                );


                /*
                 * Remaining free block.
                 */
                new_bp =
                    (char *)ptr
                    + asize
                    + NODE_SIZE;


                node =
                    NODEP(new_bp);


                node->bp =
                    new_bp;

                node->prev =
                    NULL;

                node->next =
                    NULL;


                write_block(
                    new_bp,
                    combined_size
                        - asize
                        - NODE_SIZE,
                    0
                );


                /*
                 * If consumed next was the last block,
                 * the remainder is now last.
                 */
                if (next_was_last)
                    last_block =
                        new_bp;


                list_insert(
                    node
                );
            }
            else {

                /*
                 * Entire combined region is allocated.
                 */
                if (next_was_last)
                    last_block =
                        ptr;
            }


            return ptr;
        }
    }


    /*****************************************************
     * Case 3:
     *
     * In-place growth impossible.
     *
     * Allocate elsewhere, copy, free old block.
     *****************************************************/

    {
        void *new_ptr;

        size_t old_payload;
        size_t copy_size;


        new_ptr =
            mm_malloc(size);

        if (new_ptr == NULL)
            return NULL;


        /*
         * Existing payload size.
         */
        old_payload =
            old_size - DSIZE;


        /*
         * realloc copies min(old payload, new size).
         */
        copy_size =
            old_payload < size
                ? old_payload
                : size;


        /*
         * memmove handles overlap safely.
         */
        memmove(
            new_ptr,
            ptr,
            copy_size
        );


        /*
         * Free old block.
         */
        mm_free(ptr);


        return new_ptr;
    }
}