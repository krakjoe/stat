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

#ifndef ZEND_STAT_BUFFER_H
# define ZEND_STAT_BUFFER_H

typedef struct _zend_stat_buffer_t zend_stat_buffer_t;

zend_stat_buffer_t* zend_stat_buffer_startup(zend_long slots, zend_long frequency);
void zend_stat_buffer_shutdown(zend_stat_buffer_t *);

void zend_stat_buffer_activate(zend_stat_buffer_t *buffer, pid_t pid);
void zend_stat_buffer_deactivate(zend_stat_buffer_t *buffer, pid_t pid);

#include "zend_stat_sampler.h"

zend_ulong zend_stat_buffer_interval(zend_stat_buffer_t *buffer);
double     zend_stat_buffer_started(zend_stat_buffer_t *buffer);
void zend_stat_buffer_insert(zend_stat_buffer_t *buffer, zend_stat_sample_t *sample);
void zend_stat_buffer_dump(zend_stat_buffer_t *buffer, int fd);
#endif	/* ZEND_STAT_BUFFER_H */
