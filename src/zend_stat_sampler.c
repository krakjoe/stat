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

#ifndef ZEND_STAT_SAMPLER
# define ZEND_STAT_SAMPLER

#include "zend_stat.h"
#include "zend_stat_buffer.h"
#include "zend_stat_sampler.h"

struct _zend_heap_header_t {
    int custom;
    void *storage;
    size_t size;
    size_t peak;
};

#define ZEND_STAT_ADDRESS_OFFSET(address, offset) \
            (((char*) address) + offset)
#define ZEND_STAT_ADDRESSOF(type, address, member) \
            ZEND_STAT_ADDRESS_OFFSET(address, XtOffsetOf(type, member))

#if defined(ZTS)
# if defined(TSRMG_FAST_BULK)
#   define ZEND_EXECUTOR_ADDRESS \
        ((char*) TSRMG_FAST_BULK(executor_globals_offset, zend_executor_globals*))
# else
#   define ZEND_EXECUTOR_ADDRESS \
        ((char*) TSRMG_BULK(executor_globals_id, zend_executor_globals*))
# endif
#else
#   define ZEND_EXECUTOR_ADDRESS \
        ((char*) &executor_globals)
#endif

static zend_always_inline int zend_stat_sampler_read(zend_stat_sampler_t *sampler, const void *remote, void *symbol, size_t size) { /* {{{ */
    struct iovec local;
    struct iovec target;

    local.iov_base = symbol;
    local.iov_len  = size;
    target.iov_base = (void*) remote;
    target.iov_len = size;

    if (process_vm_readv(sampler->request->pid, &local, 1, &target, 1, 0) != size) {
        return FAILURE;
    }

    return SUCCESS;
} /* }}} */

static zend_always_inline int zend_stat_sampler_read_symbol(zend_stat_sampler_t *sampler, const zend_function *remote, zend_function *local) { /* {{{ */
#ifdef ZEND_ACC_IMMUTABLE
    zend_function *cache;

    if (EXPECTED(cache = zend_hash_index_find_ptr(&sampler->cache.symbols, (zend_ulong) remote))) {
        return memcpy(local, cache, sizeof(zend_function)) == local ? SUCCESS : FAILURE;
    }
#endif

    if (UNEXPECTED(zend_stat_sampler_read(sampler, remote, local, sizeof(zend_function)) != SUCCESS)) {
        return FAILURE;
    }

#ifdef ZEND_ACC_IMMUTABLE
    if (EXPECTED(local->common.fn_flags & ZEND_ACC_IMMUTABLE)) {
        zend_hash_index_add_mem(
            &sampler->cache.symbols,
            (zend_ulong) remote,
            local, sizeof(zend_function));
    }
#endif
    return SUCCESS;
} /* }}} */

static zend_always_inline zend_stat_string_t* zend_stat_sampler_read_string(zend_stat_sampler_t *sampler, zend_string *string) { /* {{{ */
    zend_string *result;
    size_t length;

    if (EXPECTED((result = zend_hash_index_find_ptr(&sampler->cache.strings, (zend_ulong) string)))) {
        return (zend_stat_string_t*) result;
    }

    if (UNEXPECTED(zend_stat_sampler_read(sampler,
            ZEND_STAT_ADDRESSOF(zend_string, string, len),
            &length, sizeof(size_t)) != SUCCESS)) {
        return NULL;
    }

    result = zend_string_alloc(length, 1);

    if (UNEXPECTED(zend_stat_sampler_read(sampler,
            string,
            result, ZEND_MM_ALIGNED_SIZE(_ZSTR_STRUCT_SIZE(length))) != SUCCESS)) {
        pefree(result, 1);
        return NULL;
    }

    if (EXPECTED(GC_FLAGS(result) & IS_STR_PERMANENT)) {
        return zend_hash_index_add_ptr(
            &sampler->cache.strings,
            (zend_ulong) string, zend_stat_string(result));
    }

    return zend_stat_string(result);
} /* }}} */

static zend_always_inline zend_stat_string_t* zend_stat_sampler_read_string_at(zend_stat_sampler_t *sampler, const void *symbol, size_t offset) { /* {{{ */
    zend_string *string, *result;
    size_t length;

    /* Failure to read indicates the string, or symbol which contains the string, was freed:
        all of file, class, and function names should not fail since they
        cannot normally be freed during a request.
       Currently stat doesn't read any other strings ...
    */

    if (UNEXPECTED(zend_stat_sampler_read(sampler,
            ZEND_STAT_ADDRESS_OFFSET(symbol, offset),
            &string, sizeof(zend_string*)) != SUCCESS)) {
        return NULL;
    }

    return zend_stat_sampler_read_string(sampler, string);
} /* }}} */

static zend_always_inline zend_bool zend_stat_sample_unlined(zend_uchar opcode) { /* {{{ */
    /* Certain opcodes don't have useful line information because they are internal
        implementation details where a line isn't relevant normally */
    return
        opcode == ZEND_FE_FREE ||
        opcode == ZEND_FREE ||
        opcode == ZEND_ASSERT_CHECK ||
        opcode == ZEND_VERIFY_RETURN_TYPE ||
        opcode == ZEND_RECV ||
        opcode == ZEND_RECV_INIT ||
        opcode == ZEND_RECV_VARIADIC ||
        opcode == ZEND_SEND_VAL ||
        opcode == ZEND_SEND_VAR_EX ||
        opcode == ZEND_SEND_VAR_NO_REF_EX ||
        opcode == ZEND_SEND_REF ||
        opcode == ZEND_SEND_UNPACK ||
        opcode == ZEND_ROPE_INIT ||
        opcode == ZEND_ROPE_ADD ||
        opcode == ZEND_ROPE_END ||
        opcode == ZEND_FAST_CONCAT ||
        opcode == ZEND_CAST ||
        opcode == ZEND_BOOL ||
        opcode == ZEND_CASE
    ;
} /* }}} */

/* {{{ */
static zend_always_inline void zend_stat_sample(zend_stat_sampler_t *sampler) {
    zend_execute_data *fp, frame;
    zend_class_entry *scope = NULL;
    zend_function function;
    zend_op opline;
    zend_stat_sample_t sample = zend_stat_sample_empty;

    sample.elapsed = zend_stat_time();

    zend_stat_request_copy(
        &sample.request, sampler->request);

    /* This can never fail while the sampler is active */
    zend_stat_sampler_read(sampler,
        ZEND_STAT_ADDRESSOF(
            zend_heap_header_t, sampler->heap, size),
        &sample.memory, sizeof(sample.memory));

    if (UNEXPECTED((zend_stat_sampler_read(sampler,
            sampler->fp, &fp, sizeof(zend_execute_data*)) != SUCCESS) || (NULL == fp))) {
        /* There is no current execute data set */
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        goto _zend_stat_sample_return;
    }

    if (UNEXPECTED((zend_stat_sampler_read(sampler,
            fp,
            &frame, sizeof(zend_execute_data)) != SUCCESS))) {
        /* The frame was freed before it could be sampled */
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        goto _zend_stat_sample_return;
    }

    if (UNEXPECTED((NULL != frame.opline) &&
        (zend_stat_sampler_read(sampler,
            frame.opline, &opline, sizeof(zend_op)) != SUCCESS))) {
        /* The instruction pointer is in an op array that was free'd */
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        goto _zend_stat_sample_return;
    }

    if (UNEXPECTED(zend_stat_buffer_arginfo_get(sampler->buffer))) {
        sample.arginfo.length = MIN(frame.This.u2.num_args, ZEND_STAT_SAMPLE_MAX_ARGINFO);

        if (EXPECTED(sample.arginfo.length > 0)) {
            if (UNEXPECTED(zend_stat_sampler_read(sampler,
                    ZEND_CALL_ARG(fp, 1),
                    &sample.arginfo.info,
                    sizeof(zval) * sample.arginfo.length) != SUCCESS)) {
                /* The stack was freed by the sampled process, we don't bail, because
                    the rest of the sampled frame should be readable */
                sample.arginfo.length = 0;
            }
        }
    }

    /* Failures to read from here onward indicate that the sampled function has been
        or is being destroyed */

    if (UNEXPECTED(zend_stat_sampler_read_symbol(
            sampler, frame.func, &function) != SUCCESS)) {
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        memset(&sample.arginfo, 0, sizeof(sample.arginfo));

        goto _zend_stat_sample_return;
    }

    if (function.type == ZEND_USER_FUNCTION) {
        sample.location.file       =
            zend_stat_sampler_read_string(
                sampler, function.op_array.filename);

        if (UNEXPECTED(NULL == sample.location.file)) {
            sample.type = ZEND_STAT_SAMPLE_MEMORY;

            memset(&sample.arginfo,  0, sizeof(sample.arginfo));

            goto _zend_stat_sample_return;
        }

        sample.type                = ZEND_STAT_SAMPLE_USER;
        sample.location.opcode     = opline.opcode;
        if (EXPECTED(!zend_stat_sample_unlined(opline.opcode))) {
            sample.location.line   = opline.lineno;
        }
        sample.location.offset     = frame.opline - function.op_array.opcodes;
    } else {
        sample.type = ZEND_STAT_SAMPLE_INTERNAL;
    }

    if (function.common.scope) {
        sample.symbol.scope =
            zend_stat_sampler_read_string_at(
                sampler,
                function.common.scope,
                XtOffsetOf(zend_class_entry, name));

        if (UNEXPECTED(NULL == sample.symbol.scope)) {
            sample.type = ZEND_STAT_SAMPLE_MEMORY;

            memset(&sample.location, 0, sizeof(sample.location));
            memset(&sample.arginfo,  0, sizeof(sample.arginfo));

            goto _zend_stat_sample_return;
        }
    }

    sample.symbol.function =
        zend_stat_sampler_read_string(
            sampler, function.common.function_name);

    if (UNEXPECTED(NULL == sample.symbol.function)) {
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        memset(&sample.location, 0, sizeof(sample.location));
        memset(&sample.arginfo,  0, sizeof(sample.arginfo));
        memset(&sample.symbol,   0, sizeof(sample.symbol));
    }

_zend_stat_sample_return:
    zend_stat_buffer_insert(sampler->buffer, &sample);
} /* }}} */

static zend_always_inline uint32_t zend_stat_sampler_clock(uint64_t cumulative, uint64_t *ns) { /* {{{ */
    uint32_t result = 0;

    while (cumulative >= 1000000000L) {
        asm("" : "+rm"(cumulative));

        cumulative -= 1000000000L;
        result++;
    }

    *ns = cumulative;

    return result;
} /* }}} */

#ifdef ZEND_ACC_IMMUTABLE
static void zend_stat_sampler_cache_symbol_free(zval *zv) { /* {{{ */
    free(Z_PTR_P(zv));
} /* }}} */
#endif

static zend_never_inline void* zend_stat_sampler(zend_stat_sampler_t *sampler) { /* {{{ */
    struct zend_stat_sampler_timer_t
        *timer = &sampler->timer;
    struct timespec clk;

    if (clock_gettime(CLOCK_REALTIME, &clk) != SUCCESS) {
        goto _zend_stat_sampler_exit;
    }

    zend_hash_init(&sampler->cache.strings, 32, NULL, NULL, 1);
#ifdef ZEND_ACC_IMMUTABLE
    zend_hash_init(&sampler->cache.symbols, 32, NULL, zend_stat_sampler_cache_symbol_free, 1);
#endif

    pthread_mutex_lock(&timer->mutex);

    while (!timer->closed) {
        clk.tv_sec +=
            zend_stat_sampler_clock(
                clk.tv_nsec +
                    zend_stat_buffer_interval_get(sampler->buffer),
        &clk.tv_nsec);

        switch (pthread_cond_timedwait(&timer->cond, &timer->mutex, &clk)) {
            case ETIMEDOUT:
                zend_stat_sample(sampler);
            break;

            case EINVAL:
                /* clock is in the past, loop to catch up */

            case SUCCESS:
                /* do nothing */
                break;

            default:
                goto _zend_stat_sampler_leave;
        }
    }

_zend_stat_sampler_leave:
    pthread_mutex_unlock(&timer->mutex);

    zend_hash_destroy(&sampler->cache.strings);
#ifdef ZEND_ACC_IMMUTABLE
    zend_hash_destroy(&sampler->cache.symbols);
#endif

_zend_stat_sampler_exit:
    pthread_exit(NULL);
} /* }}} */

void zend_stat_sampler_activate(zend_stat_sampler_t *sampler, zend_stat_request_t *request, zend_stat_buffer_t *buffer) { /* {{{ */
    memset(sampler, 0, sizeof(zend_stat_sampler_t));

    sampler->request = request;
    sampler->buffer = buffer;
    sampler->heap =
        (zend_heap_header_t*) zend_mm_get_heap();
    sampler->fp =
        (zend_execute_data*)
            ZEND_STAT_ADDRESSOF(
                zend_executor_globals,
                ZEND_EXECUTOR_ADDRESS,
                current_execute_data);

    pthread_mutex_init(&sampler->timer.mutex, NULL);
    pthread_cond_init(&sampler->timer.cond, NULL);

    if (pthread_create(
            &sampler->timer.thread, NULL,
            (void*)(void*)
                zend_stat_sampler,
            (void*) sampler) != SUCCESS) {
        pthread_cond_destroy(&sampler->timer.cond);
        pthread_mutex_destroy(&sampler->timer.mutex);
        return;
    }

    sampler->timer.active = 1;
} /* }}} */

void zend_stat_sampler_deactivate(zend_stat_sampler_t *sampler) { /* {{{ */
    if (!sampler->timer.active) {
        return;
    }

    pthread_mutex_lock(&sampler->timer.mutex);

    sampler->timer.closed = 1;

    pthread_cond_signal(&sampler->timer.cond);
    pthread_mutex_unlock(&sampler->timer.mutex);

    pthread_join(sampler->timer.thread, NULL);

    pthread_cond_destroy(&sampler->timer.cond);
    pthread_mutex_destroy(&sampler->timer.mutex);
} /* }}} */

#endif /* ZEND_STAT_SAMPLER */
