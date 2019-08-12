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
#include "zend_stat_request.h"

extern ZEND_FUNCTION(zend_stat_sampler_activate);
extern ZEND_FUNCTION(zend_stat_sampler_active);
extern ZEND_FUNCTION(zend_stat_sampler_deactivate);

void zend_stat_sampler_auto_set(zend_bool automatic);
void zend_stat_sampler_buffer_set(zend_stat_buffer_t *buffer);
void zend_stat_sampler_interval_set(zend_long interval);
void zend_stat_sampler_limit_set(zend_long interval);
zend_long zend_stat_sampler_interval_get();
void zend_stat_sampler_arginfo_set(zend_bool arginfo);
void zend_stat_sampler_request_set(zend_stat_request_t *request);

zend_bool zend_stat_sampler_add();
void zend_stat_sampler_remove();

void zend_stat_sampler_startup(zend_bool automatic, zend_long interval, zend_bool arginfo, zend_long samplers, zend_stat_buffer_t *buffer);
void zend_stat_sampler_activate(zend_bool start);
zend_bool zend_stat_sampler_active();
void zend_stat_sampler_deactivate();
#endif	/* ZEND_STAT_SAMPLER_H */
