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

#ifndef ZEND_STAT_H
# define ZEND_STAT_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "zend.h"
#include "zend_API.h"
#include "zend_extensions.h"

#include <time.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <sys/types.h>

#include <unistd.h>
#include <pthread.h>

double zend_stat_time(void);

static zend_always_inline void* zend_stat_map(zend_long size) {
    void *mapped = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);

    if (EXPECTED(mapped != MAP_FAILED)) {
        return mapped;
    }

    return NULL;
}

static zend_always_inline void zend_stat_unmap(void *address, zend_long size) {
    if (UNEXPECTED(NULL == address)) {
        return;
    }

    munmap(address, size);
}

static zend_always_inline zend_bool zend_stat_mutex_init(pthread_mutex_t *mutex, zend_bool shared) {
    pthread_mutexattr_t attributes;

    pthread_mutexattr_init(&attributes);

    if (shared) {
        if (pthread_mutexattr_setpshared(
                &attributes, PTHREAD_PROCESS_SHARED) != SUCCESS) {
            pthread_mutexattr_destroy(&attributes);
            return 0;
        }
    }

    if (pthread_mutex_init(mutex, &attributes) != SUCCESS) {
        pthread_mutexattr_destroy(&attributes);
        return 0;
    }

    pthread_mutexattr_destroy(&attributes);
    return 1;
}

static zend_always_inline void zend_stat_mutex_destroy(pthread_mutex_t *mutex) {
    pthread_mutex_destroy(mutex);
}

static zend_always_inline zend_bool zend_stat_condition_init(pthread_cond_t *condition, zend_bool shared) {
    pthread_condattr_t attributes;

    pthread_condattr_init(&attributes);

    if (shared) {
        if (pthread_condattr_setpshared(
                &attributes, PTHREAD_PROCESS_SHARED) != SUCCESS) {
            pthread_condattr_destroy(&attributes);
            return 0;
        }
    }

    if (pthread_cond_init(condition, &attributes) != SUCCESS) {
        pthread_condattr_destroy(&attributes);
        return 0;
    }

    pthread_condattr_destroy(&attributes);
    return 1;
}

static zend_always_inline void zend_stat_condition_destroy(pthread_cond_t *condition) {
    pthread_cond_destroy(condition);
}

# if defined(ZTS) && defined(COMPILE_DL_STAT)
ZEND_TSRMLS_CACHE_EXTERN()
# endif

#define ZEND_STAT_INTERVAL_MIN 10

#endif	/* ZEND_STAT_H */
