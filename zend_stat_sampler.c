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
static void zend_stat_sample(zend_stat_sampler_t *arg) {
    zend_execute_data *frame;
    zend_class_entry *scope = NULL;
    zend_function *function = NULL;
    zend_op *opline = NULL;
    zend_stat_sample_t sample = zend_stat_sample_empty;

    sample.pid = arg->pid;
    sample.elapsed = zend_stat_time();

    zend_stat_sampler_read(arg->pid,
        ZEND_HEAP_STAT_ADDRESS(arg->heap),
        &sample.memory.size, ZEND_HEAP_STAT_LENGTH);

    if (UNEXPECTED((zend_stat_sampler_read(arg->pid,
            arg->fp, &frame, sizeof(zend_execute_data*)) != SUCCESS) || (NULL == frame))) {
        goto _zend_stat_sample_insert;
    }

    if (UNEXPECTED((zend_stat_sampler_read(arg->pid,
            (((char*) frame) + XtOffsetOf(zend_execute_data, func)),
            &function, sizeof(zend_function*)) != SUCCESS) || (NULL == function))) {
        goto _zend_stat_sample_insert;
    }

    if (UNEXPECTED(zend_stat_sampler_read(arg->pid,
            ((char*) function) + XtOffsetOf(zend_function, type),
            &sample.type, sizeof(zend_uchar)) != SUCCESS)) {
        goto _zend_stat_sample_insert;
    }

    if (sample.type == ZEND_USER_FUNCTION) {
        if (UNEXPECTED(zend_stat_sampler_read(arg->pid,
                (((char*) frame) + XtOffsetOf(zend_execute_data, opline)),
                &opline, sizeof(zend_op*)) != SUCCESS)) {
            sample.type = 0;

            goto _zend_stat_sample_insert;
        }

        if (opline) {
            if (UNEXPECTED(zend_stat_sampler_read(arg->pid,
                    (((char*) opline) + XtOffsetOf(zend_op, lineno)),
                    &sample.location.line, sizeof(uint32_t)) != SUCCESS)) {
                sample.type = 0;

                goto _zend_stat_sample_insert;
            }

            if (sample.location.line) {
                sample.location.file =
                    zend_stat_sampler_read_string(
                        arg->pid, function, XtOffsetOf(zend_op_array, filename));

                if (!sample.location.file) {
                    sample.location.line = 0;
                    sample.type = 0;
                }
            }
        }
    }

    if (UNEXPECTED(zend_stat_sampler_read(arg->pid,
            ((char*) function) + XtOffsetOf(zend_function, common.scope),
            &scope, sizeof(zend_class_entry*)) != SUCCESS)) {
        sample.type = 0;

        goto _zend_stat_sample_insert;
    } else if (scope) {
        sample.symbol.scope =
            zend_stat_sampler_read_string(
                arg->pid, scope, XtOffsetOf(zend_class_entry, name));

        if (UNEXPECTED(NULL == sample.symbol.scope)) {
            goto _zend_stat_sample_insert;
        }
    }

    sample.symbol.function =
        zend_stat_sampler_read_string(
            arg->pid, function, XtOffsetOf(zend_function, common.function_name));

    if (UNEXPECTED(NULL == sample.symbol.function)) {
        sample.symbol.scope = NULL;
    }

_zend_stat_sample_insert:
    zend_stat_buffer_insert(arg->buffer, &sample);
} /* }}} */

void zend_stat_sampler_activate(zend_stat_sampler_t *sampler, pid_t pid, zend_long interval, zend_stat_buffer_t *buffer) { /* {{{ */
    struct sigevent ev;
    struct itimerspec its;
    struct timespec ts;

    memset(sampler, 0, sizeof(zend_stat_sampler_t));

    sampler->pid = pid;
    sampler->buffer = buffer;
    sampler->heap =
        (zend_heap_header_t*) zend_mm_get_heap();
    sampler->fp = (zend_execute_data*)
        (ZEND_EXECUTOR_ADDRESS + ZEND_EXECUTOR_FRAME_OFFSET);

    memset(&ev, 0, sizeof(ev));

    ev.sigev_notify = SIGEV_THREAD;
    ev.sigev_notify_function =
        (void (*)(union sigval)) zend_stat_sample;
    ev.sigev_value.sival_ptr = sampler;

    if (timer_create(CLOCK_MONOTONIC, &ev, &sampler->timer) != SUCCESS) {
        return;
    }

    ts.tv_sec = 0;
    ts.tv_nsec = interval * 1000;

    its.it_interval = ts;
    its.it_value = ts;

    if (timer_settime(sampler->timer, 0, &its, NULL) != SUCCESS) {
        timer_delete(sampler->timer);
        return;
    }

    sampler->active = 1;
} /* }}} */

void zend_stat_sampler_deactivate(zend_stat_sampler_t *sampler) { /* {{{ */
    if (sampler->active) {
        timer_delete(sampler->timer);
    }
} /* }}} */

#endif	/* ZEND_STAT_SAMPLER */
