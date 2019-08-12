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

#include "zend_stat.h"
#include "zend_stat_buffer.h"
#include "zend_stat_sampler.h"

static zend_bool               zend_stat_sampler_auto = 1;
static zend_long               zend_stat_sampler_interval = 0;
static zend_long               zend_stat_sampler_count = 0;
static zend_long               zend_stat_sampler_limit = 0;
static zend_bool               zend_stat_sampler_arginfo = 0;

static   zend_stat_buffer_t*   zend_stat_sampler_buffer;
ZEND_TLS zend_stat_request_t   zend_stat_sampler_request;

typedef struct _zend_heap_header_t {
    int custom;
    void *storage;
    size_t size;
    size_t peak;
} zend_heap_header_t;

typedef struct _zend_stat_sampler_t {
    zend_stat_request_t *request;
    zend_stat_buffer_t  *buffer;
    struct zend_stat_sampler_timer_t {
        pthread_mutex_t mutex;
        pthread_cond_t  cond;
        zend_bool       closed;
        zend_bool       active;
        pthread_t       thread;
    } timer;
    struct {
        HashTable       strings;
        HashTable       symbols;
    } cache;
    zend_heap_header_t *heap;
    zend_execute_data  *fp;
} zend_stat_sampler_t;

ZEND_TLS zend_stat_sampler_t __sampler;

#define ZEND_STAT_SAMPLER_RESET() \
    memset(&__sampler, 0, sizeof(zend_stat_sampler_t))
#define ZEND_STAT_SAMPLER() &__sampler
#define ZSS(v) __sampler.v

/* {{{ */
void zend_stat_sampler_auto_set(zend_bool automatic) {
    __atomic_store_n(&zend_stat_sampler_auto, automatic, __ATOMIC_SEQ_CST);
}

static zend_always_inline zend_bool zend_stat_sampler_auto_get() {
    return __atomic_load_n(&zend_stat_sampler_auto, __ATOMIC_SEQ_CST);
}

void zend_stat_sampler_buffer_set(zend_stat_buffer_t *buffer) {
    zend_stat_sampler_buffer = buffer;
}

void zend_stat_sampler_arginfo_set(zend_bool arginfo) {
    __atomic_store_n(&zend_stat_sampler_arginfo, arginfo, __ATOMIC_SEQ_CST);
}

static zend_always_inline zend_bool zend_stat_sampler_arginfo_get() {
    return __atomic_load_n(&zend_stat_sampler_arginfo, __ATOMIC_SEQ_CST);
}

void zend_stat_sampler_interval_set(zend_long interval) {
    __atomic_store_n(&zend_stat_sampler_interval, interval * 1000, __ATOMIC_SEQ_CST);
}

zend_long zend_stat_sampler_interval_get() {
    return __atomic_load_n(&zend_stat_sampler_interval, __ATOMIC_SEQ_CST);
}

void zend_stat_sampler_limit_set(zend_long limit) {
    __atomic_store_n(&zend_stat_sampler_limit, limit, __ATOMIC_SEQ_CST);
}

zend_bool zend_stat_sampler_add() {
    zend_long samplers = __atomic_add_fetch(&zend_stat_sampler_limit, 1, __ATOMIC_SEQ_CST),
              limit    = __atomic_load_n(&zend_stat_sampler_count, __ATOMIC_SEQ_CST);

    if ((limit <= 0) || (samplers <= limit)) {
        return 1;
    }

    return 0;
}

void zend_stat_sampler_remove() {
    __atomic_sub_fetch(&zend_stat_sampler_count, 1, __ATOMIC_SEQ_CST);
}

void zend_stat_buffer_samplers_set(zend_long samplers) {
    __atomic_store_n(&zend_stat_sampler_limit, samplers, __ATOMIC_SEQ_CST);
}
/* }}} */

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
    zend_function *cache;

    if (EXPECTED(cache = zend_hash_index_find_ptr(&sampler->cache.symbols, (zend_ulong) remote))) {
        return memcpy(local, cache, sizeof(zend_function)) == local ? SUCCESS : FAILURE;
    }

    if (UNEXPECTED(zend_stat_sampler_read(sampler, remote, local, sizeof(zend_function)) != SUCCESS)) {
        return FAILURE;
    }

    if (
        (local->type == ZEND_INTERNAL_FUNCTION)
#ifdef ZEND_ACC_IMMUTABLE
        || (local->common.fn_flags & ZEND_ACC_IMMUTABLE)
#endif
    ) {
        zend_hash_index_add_mem(
            &sampler->cache.symbols,
            (zend_ulong) remote,
            local, sizeof(zend_function));
    }

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
    zend_string *string;

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
    zend_function function;
    zend_op opline;
    zend_stat_sample_t sample = zend_stat_sample_empty;

    sample.elapsed = zend_stat_time();

    /* This can never fail while the sampler is active */
    zend_stat_sampler_read(sampler,
        ZEND_STAT_ADDRESSOF(
            zend_heap_header_t, sampler->heap, size),
        &sample.memory, sizeof(sample.memory));

    if (UNEXPECTED((zend_stat_sampler_read(sampler,
            sampler->fp, &fp, sizeof(zend_execute_data*)) != SUCCESS) || (NULL == fp))) {
        /* There is no current execute data set */
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        goto _zend_stat_sample_finish;
    }

    if (UNEXPECTED((zend_stat_sampler_read(sampler,
            fp,
            &frame, sizeof(zend_execute_data)) != SUCCESS))) {
        /* The frame was freed before it could be sampled */
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        goto _zend_stat_sample_finish;
    }

    if (UNEXPECTED((NULL != frame.opline) &&
        (zend_stat_sampler_read(sampler,
            frame.opline, &opline, sizeof(zend_op)) != SUCCESS))) {
        /* The instruction pointer is in an op array that was free'd */
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        goto _zend_stat_sample_finish;
    }

    if (UNEXPECTED(zend_stat_sampler_arginfo_get())) {
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

        goto _zend_stat_sample_finish;
    }

    if (function.type == ZEND_USER_FUNCTION) {
        sample.symbol.file =
            zend_stat_sampler_read_string(
                sampler, function.op_array.filename);

        if (UNEXPECTED(NULL == sample.symbol.file)) {
            sample.type = ZEND_STAT_SAMPLE_MEMORY;

            memset(&sample.arginfo,  0, sizeof(sample.arginfo));

            goto _zend_stat_sample_finish;
        }

        sample.type                = ZEND_STAT_SAMPLE_USER;
        sample.location.opline.opcode     = opline.opcode;
        if (EXPECTED(!zend_stat_sample_unlined(opline.opcode))) {
            sample.location.opline.line   = opline.lineno;
        }
        sample.location.opline.offset     = frame.opline - function.op_array.opcodes;
    } else {
        zend_execute_data pframe;
        zend_function     pfunc;

        sample.type = ZEND_STAT_SAMPLE_INTERNAL;

        while (zend_stat_sampler_read(sampler,
                frame.prev_execute_data,
                &pframe, sizeof(zend_execute_data)) == SUCCESS) {
            if (EXPECTED(zend_stat_sampler_read_symbol(sampler,
                    pframe.func, &pfunc) == SUCCESS)) {
                if (pfunc.type == ZEND_USER_FUNCTION) {
                    sample.location.caller.file =
                            zend_stat_sampler_read_string(
                                sampler, pfunc.op_array.filename);

                    if (pfunc.op_array.scope) {
                        sample.location.caller.scope =
                            zend_stat_sampler_read_string_at(
                                sampler,
                                pfunc.op_array.scope,
                                XtOffsetOf(zend_class_entry, name));
                        if (UNEXPECTED(NULL == sample.location.caller.scope)) {
                            break;
                        }
                    }

                    sample.location.caller.function =
                        zend_stat_sampler_read_string(sampler, pfunc.op_array.function_name);
                    break;
                }
            } else {
                break;
            }
            frame = pframe;
        }
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

            goto _zend_stat_sample_finish;
        }
    }

    sample.symbol.function =
        zend_stat_sampler_read_string(
            sampler, function.common.function_name);

_zend_stat_sample_finish:
    /* This is just a memcpy and some adds,
        request data is refcounted. */
    zend_stat_request_copy(
        &sample.request, sampler->request);

    zend_stat_buffer_insert(sampler->buffer, &sample);
} /* }}} */

static zend_always_inline time_t zend_stat_sampler_clock(long cumulative, long *ns) { /* {{{ */
    time_t result = 0;

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
                    zend_stat_sampler_interval_get(),
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

void zend_stat_sampler_startup( /* {{{ */
        zend_bool automatic,
        zend_long interval,
        zend_bool arginfo,
        zend_long samplers,
        zend_stat_buffer_t *buffer) {

    zend_stat_sampler_auto_set(automatic);
    zend_stat_sampler_interval_set(interval);
    zend_stat_sampler_arginfo_set(arginfo);
    zend_stat_sampler_limit_set(samplers);

    zend_stat_sampler_buffer_set(buffer);
} /* }}} */

ZEND_FUNCTION(zend_stat_sampler_activate) /* {{{ */
{
    ZEND_PARSE_PARAMETERS_START(0, 0)
    ZEND_PARSE_PARAMETERS_END();

    if (UNEXPECTED(1 == zend_stat_sampler_active())) {
        RETURN_FALSE;
    }

    zend_stat_sampler_activate(1);

    RETURN_BOOL(zend_stat_sampler_active());
} /* }}} */

void zend_stat_sampler_activate(zend_bool start) { /* {{{ */
    if ((0 == zend_stat_sampler_auto_get()) && (0 == start)) {
        return;
    }

    if (!zend_stat_sampler_add()) {
        return;
    }

    if (!zend_stat_request_create(&zend_stat_sampler_request)) {
        zend_error(E_WARNING,
            "[STAT] Could not allocate request, "
            "not activating sampler, may be low on memory");
        return;
    }
    
    ZEND_STAT_SAMPLER_RESET();

    ZSS(request) = &zend_stat_sampler_request;
    ZSS(buffer) = zend_stat_sampler_buffer;
    ZSS(heap) =
        (zend_heap_header_t*) zend_mm_get_heap();
    ZSS(fp) =
        (zend_execute_data*)
            ZEND_STAT_ADDRESSOF(
                zend_executor_globals,
                ZEND_EXECUTOR_ADDRESS,
                current_execute_data);

    if (!zend_stat_mutex_init(&ZSS(timer).mutex, 0) ||
        !zend_stat_condition_init(&ZSS(timer).cond, 0)) {
        return;
    }

    if (pthread_create(
            &ZSS(timer).thread, NULL,
            (void*)(void*)
                zend_stat_sampler,
            (void*) ZEND_STAT_SAMPLER()) != SUCCESS) {
        pthread_cond_destroy(&ZSS(timer).cond);
        pthread_mutex_destroy(&ZSS(timer).mutex);
        return;
    }

    ZSS(timer).active = 1;
} /* }}} */

ZEND_FUNCTION(zend_stat_sampler_active) /* {{{ */
{
    ZEND_PARSE_PARAMETERS_START(0, 0)
    ZEND_PARSE_PARAMETERS_END();

    RETURN_BOOL(zend_stat_sampler_active());  
} /* }}} */

zend_bool zend_stat_sampler_active() { /* {{{ */
    return ZSS(timer).active;
} /* }}} */

ZEND_FUNCTION(zend_stat_sampler_deactivate) /* {{{ */
{
    ZEND_PARSE_PARAMETERS_START(0, 0)
    ZEND_PARSE_PARAMETERS_END();

    if (UNEXPECTED(0 == zend_stat_sampler_active())) {
        RETURN_FALSE;
    }

    zend_stat_sampler_deactivate();

    RETURN_TRUE;
} /* }}} */

void zend_stat_sampler_deactivate() { /* {{{ */
    if (0 == zend_stat_sampler_active()) {
        return;
    }

    pthread_mutex_lock(&ZSS(timer).mutex);

    ZSS(timer).closed = 1;

    pthread_cond_signal(&ZSS(timer).cond);
    pthread_mutex_unlock(&ZSS(timer).mutex);

    pthread_join(ZSS(timer).thread, NULL);

    zend_stat_condition_destroy(&ZSS(timer).cond);
    zend_stat_mutex_destroy(&ZSS(timer).mutex);

    zend_stat_request_release(&zend_stat_sampler_request);

    zend_stat_sampler_remove();

    ZEND_STAT_SAMPLER_RESET();
} /* }}} */

#endif /* ZEND_STAT_SAMPLER */
