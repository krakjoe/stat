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

#ifndef ZEND_STAT_SAMPLER_H
# define ZEND_STAT_SAMPLER_H

#include "zend_stat_sample.h"

typedef struct _zend_heap_header_t zend_heap_header_t;

typedef struct _zend_stat_sampler_t {
    pid_t               pid;
    struct zend_stat_sampler_timer_t {
        pthread_mutex_t mutex;
        pthread_cond_t  cond;
        zend_bool       closed;
        zend_bool       active;
        pthread_t       thread;
    } timer;
    struct {
        HashTable       strings;
#ifdef ZEND_ACC_IMMUTABLE
        HashTable       symbols;
#endif
    } cache;
    zend_bool           arginfo;
    zend_stat_buffer_t *buffer;
    zend_heap_header_t *heap;
    zend_execute_data  *fp;
} zend_stat_sampler_t;

void zend_stat_sampler_activate(zend_stat_sampler_t *sampler, pid_t pid, zend_stat_buffer_t *buffer);
void zend_stat_sampler_deactivate(zend_stat_sampler_t *sampler);
#endif	/* ZEND_STAT_SAMPLER_H */
