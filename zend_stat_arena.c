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

struct _zend_stat_arena_t {
    pthread_mutex_t               mutex;
    zend_long                     size;
    zend_long                     brk;
    zend_stat_arena_block_t      *mem;
    zend_stat_arena_block_t      *start;
    zend_stat_arena_block_t      *end;
};

static zend_always_inline zend_long zend_stat_arena_aligned(zend_long size) {
    return (size + sizeof(zend_stat_arena_ptr_t) - 1) & ~(sizeof(zend_stat_arena_ptr_t) - 1);
}

static zend_always_inline zend_stat_arena_block_t* zend_stat_arena_block(void *mem) {
    return (zend_stat_arena_block_t*) (((char*) mem) - XtOffsetOf(zend_stat_arena_block_t, mem));
}

static zend_always_inline zend_stat_arena_block_t* zend_stat_arena_find(zend_stat_arena_t *arena, zend_long size) {
    zend_stat_arena_block_t *block = arena->start;

    while (NULL != block) {
        if ((block->size >= size) && (0 == block->used)) {
            block->used = 1;

            break;
        }

        block = block->next;
    }

    return block;
}

zend_stat_arena_t* zend_stat_arena_create(zend_long size) {
    pthread_mutexattr_t attr;
    zend_long aligned =
        zend_stat_arena_aligned(sizeof(zend_stat_arena_t) + size);
    zend_stat_arena_t *arena =
        (zend_stat_arena_t*)
            zend_stat_map(aligned);

    if (!arena) {
        return NULL;
    }

    memset(arena, 0, aligned);

    arena->size = aligned;
    arena->mem =
        (zend_stat_arena_block_t*)
            (((char*) arena) + sizeof(zend_stat_arena_t));

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(
        &attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&arena->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    return arena;
}

void* zend_stat_arena_alloc(zend_stat_arena_t *arena, zend_long size) {
    zend_long aligned =
        zend_stat_arena_aligned(
            sizeof(zend_stat_arena_block_t) + size);
    zend_stat_arena_block_t *block;

    if (pthread_mutex_lock(&arena->mutex) != SUCCESS) {
        return NULL;
    }

    block = zend_stat_arena_find(arena, aligned);

    if (EXPECTED(NULL != block)) {
        goto _zend_stat_arena_alloc_leave;
    }

    if (UNEXPECTED((arena->brk + aligned) > arena->size)) {
        /* OOM */
        goto _zend_stat_arena_alloc_leave;
    }

    block =
        (zend_stat_arena_block_t*)
            (((char*) arena->mem) + arena->brk);
    block->used = 1;
    block->size = aligned;

    if (UNEXPECTED(NULL == arena->start)) {
        arena->start = block;
    }

    if (EXPECTED(NULL != arena->end)) {
        arena->end->next = block;
    }

    arena->end = block;
    arena->brk += aligned;

_zend_stat_arena_alloc_leave:
    pthread_mutex_unlock(&arena->mutex);

    return block->mem;
}

void zend_stat_arena_free(zend_stat_arena_t *arena, void *mem) {
    zend_stat_arena_block_t *block = zend_stat_arena_block(mem);

    pthread_mutex_lock(&arena->mutex);

    block->used = 0;

    pthread_mutex_unlock(&arena->mutex);
}

void zend_stat_arena_destroy(zend_stat_arena_t *arena) {
    if (!arena) {
        return;
    }

    pthread_mutex_destroy(&arena->mutex);

    zend_stat_unmap(arena, arena->size);
}
#endif
