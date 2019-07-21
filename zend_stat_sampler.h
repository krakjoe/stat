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

#include "zend_stat_strings.h"

typedef struct _zend_stat_sample_state_t {
    zend_bool                busy;
    zend_bool                used;
} zend_stat_sample_state_t;

typedef struct _zend_stat_sample_t {
    zend_stat_sample_state_t state;
    zend_uchar               type;
    pid_t                    pid;
    double                   elapsed;
    struct {
        zend_stat_string_t  *file;
        uint32_t             line;
    } location;
    zend_stat_string_t      *scope;
    zend_stat_string_t      *function;
} zend_stat_sample_t;

#define ZEND_STAT_SAMPLE_DATA(s) \
    (((char*) s) + XtOffsetOf(zend_stat_sample_t, type))
#define ZEND_STAT_SAMPLE_DATA_SIZE \
    (sizeof(zend_stat_sample_t) - XtOffsetOf(zend_stat_sample_t, type))

void zend_stat_sampler_activate(zend_stat_buffer_t *buffer, pid_t pid);
void zend_stat_sampler_deactivate(zend_stat_buffer_t *buffer, pid_t pid);
#endif	/* ZEND_STAT_SAMPLER_H */
