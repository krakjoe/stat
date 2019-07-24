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

#if defined(ZTS)
# if defined(TSRMG_FAST_BULK)
#   define ZEND_EXECUTOR_ADDRESS ((char*) TSRMG_FAST_BULK(executor_globals_offset, zend_executor_globals*))
# else
#   define ZEND_EXECUTOR_ADDRESS ((char*) TSRMG_BULK(executor_globals_id, zend_executor_globals*))
# endif
#else
#   define ZEND_EXECUTOR_ADDRESS ((char*) &executor_globals)
#endif

#define ZEND_EXECUTOR_FRAME_OFFSET XtOffsetOf(zend_executor_globals, current_execute_data)

#define ZEND_HEAP_STAT_ADDRESS(heap) (((char*) heap) + XtOffsetOf(zend_heap_header_t, size))
#define ZEND_HEAP_STAT_LENGTH (sizeof(size_t) * 2)

static zend_always_inline int zend_stat_sampler_read(pid_t pid, const void *remote, void *symbol, size_t size) { /* {{{ */
    struct iovec local;
    struct iovec target;

    local.iov_base = symbol;
    local.iov_len  = size;
    target.iov_base = (void*) remote;
    target.iov_len = size;

    if (process_vm_readv(pid, &local, 1, &target, 1, 0) != size) {
        return FAILURE;
    }

    return SUCCESS;
} /* }}} */

static zend_always_inline zend_stat_string_t* zend_stat_sampler_read_string(pid_t pid, const void *symbol, size_t offset) { /* {{{ */
    zend_string *string, *result;
    size_t length;

    if (zend_stat_sampler_read(pid,
            (((char*) symbol) + offset),
            &string, sizeof(zend_string*)) != SUCCESS) {
        return NULL;
    }

    if (zend_stat_sampler_read(pid,
            (((char*) string) + XtOffsetOf(zend_string, len)),
            &length, sizeof(size_t)) != SUCCESS) {
        return NULL;
    }

    result = zend_string_alloc(length, 1);

    if (zend_stat_sampler_read(pid,
            string,
            result, ZEND_MM_ALIGNED_SIZE(_ZSTR_STRUCT_SIZE(length))) != SUCCESS) {
        pefree(result, 1);
        return NULL;
    }

    return zend_stat_string(result);

} /* }}} */

/* {{{ */
static zend_always_inline void zend_stat_sample(zend_stat_sampler_t *arg) {
    zend_execute_data *frame;
    zend_class_entry *scope = NULL;
    zend_function *function = NULL;
    zend_op *opline = NULL;
    zend_stat_sample_t sample = zend_stat_sample_empty;

    sample.pid = arg->pid;
    sample.elapsed = zend_stat_time();

    zend_stat_sampler_read(arg->pid,
        ZEND_HEAP_STAT_ADDRESS(arg->heap),
        &sample.memory.used, ZEND_HEAP_STAT_LENGTH);

    if (UNEXPECTED((zend_stat_sampler_read(arg->pid,
            arg->fp, &frame, sizeof(zend_execute_data*)) != SUCCESS) || (NULL == frame))) {
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        goto _zend_stat_sample_insert;
    }

    if (UNEXPECTED((zend_stat_sampler_read(arg->pid,
            (((char*) frame) + XtOffsetOf(zend_execute_data, func)),
            &function, sizeof(zend_function*)) != SUCCESS) || (NULL == function))) {
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        goto _zend_stat_sample_insert;
    }

    if (UNEXPECTED(zend_stat_sampler_read(arg->pid,
            ((char*) function) + XtOffsetOf(zend_function, type),
            &sample.type, sizeof(zend_uchar)) != SUCCESS)) {
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        goto _zend_stat_sample_insert;
    }

    if (sample.type == ZEND_USER_FUNCTION) {
        if (UNEXPECTED(zend_stat_sampler_read(arg->pid,
                (((char*) frame) + XtOffsetOf(zend_execute_data, opline)),
                &opline, sizeof(zend_op*)) != SUCCESS)) {
            sample.type = ZEND_STAT_SAMPLE_MEMORY;

            goto _zend_stat_sample_insert;
        }

        if (opline) {
            if (UNEXPECTED(zend_stat_sampler_read(arg->pid,
                    (((char*) opline) + XtOffsetOf(zend_op, lineno)),
                    &sample.location.line, sizeof(uint32_t)) != SUCCESS)) {
                sample.type = ZEND_STAT_SAMPLE_MEMORY;

                goto _zend_stat_sample_insert;
            }

            if (sample.location.line) {
                sample.location.file =
                    zend_stat_sampler_read_string(
                        arg->pid, function, XtOffsetOf(zend_op_array, filename));

                if (!sample.location.file) {
                    sample.type = ZEND_STAT_SAMPLE_MEMORY;
                }
            }
        }
    } else {
        sample.type = ZEND_STAT_SAMPLE_INTERNAL;
    }

    if (UNEXPECTED(zend_stat_sampler_read(arg->pid,
            ((char*) function) + XtOffsetOf(zend_function, common.scope),
            &scope, sizeof(zend_class_entry*)) != SUCCESS)) {
        sample.type = ZEND_STAT_SAMPLE_MEMORY;

        goto _zend_stat_sample_insert;
    } else if (scope) {
        sample.symbol.scope =
            zend_stat_sampler_read_string(
                arg->pid, scope, XtOffsetOf(zend_class_entry, name));

        if (UNEXPECTED(NULL == sample.symbol.scope)) {
            sample.type = ZEND_STAT_SAMPLE_MEMORY;

            goto _zend_stat_sample_insert;
        }
    }

    sample.symbol.function =
        zend_stat_sampler_read_string(
            arg->pid, function, XtOffsetOf(zend_function, common.function_name));

    if (UNEXPECTED(NULL == sample.symbol.function)) {
        sample.type = ZEND_STAT_SAMPLE_MEMORY;
    }

_zend_stat_sample_insert:
    zend_stat_buffer_insert(arg->buffer, &sample);
} /* }}} */

static zend_always_inline uint32_t zend_stat_sampler_timer_increment(uint64_t cumulative, uint64_t *ns) { /* {{{ */
    uint32_t result = 0;

    while (cumulative >= 1000000000L) {
        asm("" : "+rm"(cumulative));

        cumulative -= 1000000000L;
        result++;
    }

    *ns = cumulative;

    return result;
} /* }}} */

static void* zend_stat_sampler(zend_stat_sampler_t *sampler) { /* {{{ */
    struct zend_stat_sampler_timer_t
        *timer = &sampler->timer;
    struct timespec delay;

    pthread_mutex_lock(&timer->mutex);

    while (!timer->closed) {
        if (clock_gettime(CLOCK_REALTIME, &delay) != SUCCESS) {
            break;
        }

        delay.tv_sec +=
            zend_stat_sampler_timer_increment(
                delay.tv_nsec +
                    timer->interval,
        &delay.tv_nsec);

        switch (pthread_cond_timedwait(&timer->cond, &timer->mutex, &delay)) {
            case ETIMEDOUT:
            case EINVAL:
                zend_stat_sample(sampler);
            break;

            case SUCCESS:
                /* do nothing */
                break;

            default:
                goto _zend_stat_sampler_routine_leave;
        }
    }

_zend_stat_sampler_routine_leave:
    pthread_mutex_unlock(&timer->mutex);

    pthread_exit(NULL);
} /* }}} */

void zend_stat_sampler_activate(zend_stat_sampler_t *sampler, pid_t pid, zend_long interval, zend_stat_buffer_t *buffer) { /* {{{ */
    memset(sampler, 0, sizeof(zend_stat_sampler_t));

    sampler->pid = pid;
    sampler->buffer = buffer;
    sampler->heap =
        (zend_heap_header_t*) zend_mm_get_heap();
    sampler->fp = (zend_execute_data*)
        (ZEND_EXECUTOR_ADDRESS + ZEND_EXECUTOR_FRAME_OFFSET);

    sampler->timer.interval = interval * 1000;

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

#endif	/* ZEND_STAT_SAMPLER */
