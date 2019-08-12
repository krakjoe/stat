/*
  +----------------------------------------------------------------------+
  | stat                                                                |
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

#ifndef ZEND_STAT_STRINGS
# define ZEND_STAT_STRINGS

#include "zend_stat.h"
#include "zend_stat_arena.h"
#include "zend_stat_strings.h"

typedef struct {
    zend_long size;
    zend_long used;
    zend_long slots;
    struct {
        void *memory;
        zend_long size;
        zend_long used;
    } buffer;
    zend_stat_arena_t  *arena;
    zend_stat_string_t *strings;
} zend_stat_strings_t;

static zend_stat_strings_t* zend_stat_strings;
static zend_stat_string_t*  zend_stat_strings_opcodes[256];

#define ZTSG(v) zend_stat_strings->v
#define ZTSB(v) ZTSG(buffer).v

static zend_always_inline zend_stat_string_t* zend_stat_string_init(const char *value, size_t length) {
    zend_stat_string_t *string;
    zend_ulong slot;
    zend_ulong offset;
    zend_ulong hash = zend_inline_hash_func(value, length);

    if (UNEXPECTED(++ZTSG(used) >= ZTSG(slots))) {
        --ZTSG(used);

        return NULL;
    }

    slot = hash % ZTSG(slots);

_zend_stat_string_init_load:
    string = &ZTSG(strings)[slot];

    if (UNEXPECTED(string->hash == hash)) {
        return string;
    } else {
        if (UNEXPECTED(string->length)) {
            slot++;
            slot %= ZTSG(slots);

            goto _zend_stat_string_init_load;
        }
    }

    offset = ZTSB(used);

    if (UNEXPECTED((offset + length) >= ZTSB(size))) {
        /* panic OOM */

        return NULL;
    }

    string->value = (char*) (((char*) ZTSB(memory)) + offset);

    memcpy(string->value, value, length);

    string->value[length] = 0;
    string->hash = hash;
    string->length = length;

    ZTSB(used) += length;

    return string;
}

static zend_always_inline zend_stat_string_t* zend_stat_string_persistent(zend_string *string) {
    zend_stat_string_t *copy;
    zend_ulong slot;
    zend_ulong offset;

    if (UNEXPECTED(__atomic_add_fetch(
            &ZTSG(used), 1, __ATOMIC_ACQ_REL) >= ZTSG(slots))) {
        __atomic_sub_fetch(&ZTSG(used), 1, __ATOMIC_ACQ_REL);

        /* panic OOM */

        return NULL;
    }

    slot = ZSTR_HASH(string) % ZTSG(slots);

_zend_stat_string_copy_load:
    copy = &ZTSG(strings)[slot];

    if (EXPECTED(__atomic_load_n(&copy->length, __ATOMIC_SEQ_CST))) {
_zend_stat_string_copy_check:
        if (UNEXPECTED(copy->hash != ZSTR_HASH(string))) {
            ++slot;

            slot %= ZTSG(slots);

            goto _zend_stat_string_copy_load;
        }

        __atomic_sub_fetch(
            &ZTSG(used), 1, __ATOMIC_ACQ_REL);

        free(string);

        return copy;
    }

    while (__atomic_exchange_n(&copy->locked, 1, __ATOMIC_RELAXED));

    __atomic_thread_fence(__ATOMIC_ACQUIRE);

    offset = __atomic_fetch_add(&ZTSB(used), ZSTR_LEN(string), __ATOMIC_ACQ_REL);

    if (UNEXPECTED((offset + ZSTR_LEN(string)) >= ZTSB(size))) {
        __atomic_sub_fetch(&ZTSB(used), ZSTR_LEN(string), __ATOMIC_ACQ_REL);
        __atomic_thread_fence(__ATOMIC_RELEASE);
        __atomic_exchange_n(&copy->locked, 0, __ATOMIC_RELAXED);

        /* panic OOM */

        return NULL;
    }

    if (UNEXPECTED(__atomic_load_n(&copy->length, __ATOMIC_SEQ_CST))) {
        __atomic_thread_fence(__ATOMIC_RELEASE);
        __atomic_exchange_n(&copy->locked, 0, __ATOMIC_RELAXED);

        goto _zend_stat_string_copy_check;
    }

    copy->value = (char*) (((char*) ZTSB(memory)) + offset);

    memcpy(copy->value,
           ZSTR_VAL(string),
           ZSTR_LEN(string));

    copy->value[ZSTR_LEN(string)] = 0;
    copy->hash = ZSTR_HASH(string);

    __atomic_store_n(&copy->length, ZSTR_LEN(string), __ATOMIC_SEQ_CST);

    __atomic_thread_fence(__ATOMIC_RELEASE);

    __atomic_exchange_n(&copy->locked, 0, __ATOMIC_RELAXED);

    free(string);

    return copy;
}

zend_bool zend_stat_strings_startup(zend_long strings) {
    size_t zend_stat_strings_size = floor((strings / 5) * 1),
           zend_stat_strings_buffer_size = floor((strings / 5) * 4);

    zend_stat_strings = zend_stat_map(strings + sizeof(zend_stat_strings_t));

    if (!zend_stat_strings) {
        zend_error(E_WARNING,
            "[STAT] Failed to allocate shared memory for strings");
        return 0;
    }

    memset(zend_stat_strings, 0, sizeof(zend_stat_strings_t));

    ZTSG(strings) = (void*)
                        (((char*) zend_stat_strings) + sizeof(zend_stat_strings_t));
    ZTSG(size)    = zend_stat_strings_size;
    ZTSG(slots)   = ZTSG(size) / sizeof(zend_stat_string_t);
    ZTSG(used)    = 0;

    memset(ZTSG(strings), 0, zend_stat_strings_size);

    ZTSB(memory)  = (void*)
                        (((char*) ZTSG(strings)) + zend_stat_strings_size);
    ZTSB(size)    = zend_stat_strings_buffer_size;
    ZTSB(used)    = 0;

    memset(ZTSB(memory), 0, zend_stat_strings_buffer_size);

    {
        int it = 0,
            end = ZEND_VM_LAST_OPCODE;

        memset(zend_stat_strings_opcodes, 0, sizeof(zend_stat_strings_opcodes));

        while (it <= end) {
            const char *name = zend_get_opcode_name(it);

            if (name) {
                zend_stat_strings_opcodes[it] =
                    zend_stat_string_init(
                        (char*) name + (sizeof("ZEND_")-1),
                        strlen(name) - (sizeof("ZEND_")-1));
            } else {
                zend_stat_strings_opcodes[it] =
                    zend_stat_string_init(ZEND_STRL("UNKNOWN"));
            }
            it++;
        }
    }

    ZTSG(arena) = zend_stat_arena_create(strings);

    return 1;
}

zend_stat_string_t* zend_stat_string_temporary(const char *value, size_t length) {
    size_t size = sizeof(zend_stat_string_t) + (length + 1);
    zend_stat_string_t *temporary =
        (zend_stat_string_t*)
            zend_stat_arena_alloc(ZTSG(arena), size);

    if (UNEXPECTED(NULL == temporary)) {
        return NULL;
    }

    memset(temporary, 0, size);

    temporary->u.type = ZEND_STAT_STRING_TEMPORARY;
    temporary->u.refcount = 1;

    temporary->length = length;
    temporary->value =
        (char*) (((char*) temporary) + sizeof(zend_stat_string_t));
    memcpy(temporary->value, value, length);

    temporary->value[length] = 0;

    return temporary;
}

zend_stat_string_t* zend_stat_string_copy(zend_stat_string_t *string) {
    if (UNEXPECTED(ZEND_STAT_STRING_PERSISTENT == __atomic_load_n(&string->u.type, __ATOMIC_SEQ_CST))) {
        return string;
    }

    __atomic_add_fetch(
        &string->u.refcount,
        1, __ATOMIC_SEQ_CST);

    return string;
}

void zend_stat_string_release(zend_stat_string_t *string) {
    if (UNEXPECTED(__atomic_load_n(&string->u.type, __ATOMIC_SEQ_CST) == ZEND_STAT_STRING_PERSISTENT)) {
        return;
    }

    if (__atomic_sub_fetch(&string->u.refcount, 1, __ATOMIC_SEQ_CST) == 0) {
        zend_stat_arena_free(ZTSG(arena), string);
        return;
    }
}

zend_stat_string_t *zend_stat_string_opcode(zend_uchar opcode) {
    return zend_stat_strings_opcodes[opcode];
}

zend_stat_string_t *zend_stat_string(zend_string *string) {
    return zend_stat_string_persistent(string);
}

void zend_stat_strings_shutdown(void) {
    zend_stat_arena_destroy(ZTSG(arena));

    zend_stat_unmap(zend_stat_strings, ZTSG(size));
}

#endif	/* ZEND_STAT_STRINGS */
