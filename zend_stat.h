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

double zend_stat_time(void);

static zend_always_inline void* zend_stat_map(zend_long size) {
    void *mapped = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, 0, 0);

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

# if defined(ZTS) && defined(COMPILE_DL_STAT)
ZEND_TSRMLS_CACHE_EXTERN()
# endif

#endif	/* ZEND_STAT_H */
