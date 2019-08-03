/*
  +----------------------------------------------------------------------+
  | stat                                                                 |
  +----------------------------------------------------------------------+
  | Copyright (c) Joe Watkins 2019                                       |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: krakjoe                                                      |
  +----------------------------------------------------------------------+
 */

#ifndef ZEND_STAT_ARENA
# define ZEND_STAT_ARENA

#include "zend_stat.h"
#include "zend_stat_arena.h"

typedef struct _zend_stat_arena_block_t zend_stat_arena_block_t;

typedef intptr_t zend_stat_arena_ptr_t;

struct _zend_stat_arena_block_t {
    zend_long                    size;
    zend_bool                    used;
    zend_stat_arena_block_t     *next;
    zend_stat_arena_ptr_t        mem[1];
};

#define ZEND_STAT_ARENA_BLOCK_SIZE \
    zend_stat_arena_aligned(sizeof(zend_stat_arena_block_t))
#define ZEND_STAT_ARENA_BLOCK_MIN \
    (ZEND_STAT_ARENA_BLOCK_SIZE * 2)

struct _zend_stat_arena_t {
    pthread_mutex_t               mutex;
    zend_long                     size;
    zend_ulong                    bytes;
    char                         *brk;
    char                         *end;
    struct {
        zend_stat_arena_block_t  *start;
        zend_stat_arena_block_t  *end;
    } list;
};

#define ZEND_STAT_ARENA_SIZE \
    zend_stat_arena_aligned(sizeof(zend_stat_arena_t))
#define ZEND_STAT_ARENA_OOM \
    (void*) -1

static zend_always_inline zend_long zend_stat_arena_aligned(zend_long size) {
    return (size + sizeof(zend_stat_arena_ptr_t) - 1) & ~(sizeof(zend_stat_arena_ptr_t) - 1);
}

static zend_always_inline zend_stat_arena_block_t* zend_stat_arena_block(void *mem) {
    return (zend_stat_arena_block_t*) (((char*) mem) - XtOffsetOf(zend_stat_arena_block_t, mem));
}

static zend_always_inline zend_stat_arena_block_t* zend_stat_arena_find(zend_stat_arena_t *arena, zend_long size) {
    zend_stat_arena_block_t *block = arena->list.start;
    zend_long wasted;

    while (NULL != block) {
        if ((0 == block->used)) {
            if ((block->size >= size)) {
                block->used = 1;
                goto _zend_stat_arena_found;
            } else {
                if (NULL != block->next) {
                    if ((0 == block->next->used) &&
                        ((block->size + block->next->size) >= size)) {
                        block->size += block->next->size;
                        block->next = block->next->next;
                        block->used = 1;
                        goto _zend_stat_arena_found;
                    }
                }
            }
        }

        block = block->next;
    }

    return NULL;

_zend_stat_arena_found:
    if ((NULL != block) &&
        ((wasted = (block->size - size)) > 0)) {
        if ((wasted > ZEND_STAT_ARENA_BLOCK_MIN)) {
            zend_stat_arena_block_t *reclaim =
                (zend_stat_arena_block_t*)
                    (((char*) block->mem) + size);

            reclaim->used = 0;
            reclaim->size =
                (wasted - ZEND_STAT_ARENA_BLOCK_SIZE);
            reclaim->next = block->next;

            block->next  = reclaim;
        }

        block->size = size;
    }

    return block;
}

zend_stat_arena_t* zend_stat_arena_create(zend_long size) {
    zend_long aligned =
        zend_stat_arena_aligned(ZEND_STAT_ARENA_SIZE + size);
    zend_stat_arena_t *arena =
        (zend_stat_arena_t*)
            zend_stat_map(aligned);

    if (!arena) {
        return NULL;
    }

    if (!zend_stat_mutex_init(&arena->mutex, 1)) {
        zend_stat_unmap(arena, aligned);
        return NULL;
    }

    arena->end = (char*) (((char*) arena) + aligned);
    arena->brk = (char*) (((char*) arena) + ZEND_STAT_ARENA_SIZE);
    arena->bytes = arena->end - arena->brk;
    arena->size = aligned;

    return arena;
}

static zend_stat_arena_block_t* zend_stat_arena_brk(zend_stat_arena_t *arena, zend_long size) {
    if (brk > 0) {
        size =
            zend_stat_arena_aligned(
                ZEND_STAT_ARENA_BLOCK_SIZE + size);

        if (UNEXPECTED((arena->brk + size) > arena->end)) {
            return ZEND_STAT_ARENA_OOM;
        }

        arena->brk += size;
    }

    return (zend_stat_arena_block_t*) arena->brk;
}

void* zend_stat_arena_alloc(zend_stat_arena_t *arena, zend_long size) {
    zend_long aligned =
        zend_stat_arena_aligned(size);
    zend_stat_arena_block_t *block;

    if (UNEXPECTED(SUCCESS !=
            pthread_mutex_lock(&arena->mutex))) {
        return NULL;
    }

    block = zend_stat_arena_find(arena, aligned);

    if (EXPECTED(NULL != block)) {
        goto _zend_stat_arena_alloc_leave;
    }

    block = zend_stat_arena_brk(arena, 0);

    if (UNEXPECTED(zend_stat_arena_brk(
            arena, aligned) == ZEND_STAT_ARENA_OOM)) {
        goto _zend_stat_arena_alloc_oom;
    }

    block->used = 1;
    block->size = aligned;

    if (UNEXPECTED(NULL == arena->list.start)) {
        arena->list.start = block;
    }

    if (EXPECTED(NULL != arena->list.end)) {
        arena->list.end->next = block;
    }

    arena->list.end = block;

_zend_stat_arena_alloc_leave:
    pthread_mutex_unlock(&arena->mutex);

    return memset(block->mem, 0, block->size);

_zend_stat_arena_alloc_oom:
    pthread_mutex_unlock(&arena->mutex);

    return NULL;
}

#ifdef ZEND_DEBUG
static zend_always_inline zend_bool zend_stat_arena_owns(zend_stat_arena_t *arena, void *mem) {
    if (UNEXPECTED((((char*) mem) < ((char*) arena)) || (((char*) mem) > arena->end))) {
        return 0;
    }

    return 1;
}
#endif

void zend_stat_arena_free(zend_stat_arena_t *arena, void *mem) {
    zend_stat_arena_block_t *block = zend_stat_arena_block(mem);

#ifdef ZEND_DEBUG
    ZEND_ASSERT(zend_stat_arena_owns(arena, mem));
#endif

    /* Currently this is not a very high frequency function, it is only
        ever invoked by rshutdown. */
    pthread_mutex_lock(&arena->mutex);

    while ((NULL != block->next)) {
        if ((0 == block->next->used)) {
            if ((NULL != arena->list.end) &&
                (block->next == arena->list.end)) {
                arena->list.end = block->next->next;
            }

            block->size += block->next->size;
            block->next = block->next->next;
        } else {
            break;
        }
    }

    block->used = 0;

    pthread_mutex_unlock(&arena->mutex);
}

#ifdef ZEND_DEBUG
static zend_always_inline void zend_stat_arena_debug(zend_stat_arena_t *arena) {
    zend_stat_arena_block_t *block = arena->list.start;

    while (NULL != block) {
        if (block->used) {
            fprintf(stderr,
                "[STAT] %p leaked "ZEND_LONG_FMT" bytes\n",
                block->mem, block->size);
        }
        block = block->next;
    }
}
#endif

void zend_stat_arena_destroy(zend_stat_arena_t *arena) {
    if (!arena) {
        return;
    }

#ifdef ZEND_DEBUG
    zend_stat_arena_debug(arena);
#endif

    zend_stat_mutex_destroy(&arena->mutex);

    zend_stat_unmap(arena, arena->size);
}
#endif
