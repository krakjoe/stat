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

#ifndef ZEND_STAT_REQUEST_H
# define ZEND_STAT_REQUEST_H

#include "zend_stat_strings.h"

typedef struct _zend_stat_request_t {
    pid_t               pid;
    double              elapsed;
    zend_stat_string_t *method;
    zend_stat_string_t *uri;

} zend_stat_request_t;

zend_bool zend_stat_request_create(zend_stat_request_t *request);

static zend_always_inline void zend_stat_request_copy(zend_stat_request_t *dest, zend_stat_request_t *src) {
    memcpy(dest, src, sizeof(zend_stat_request_t));

    if (dest->method) {
        dest->method = zend_stat_string_copy(dest->method);
    }

    if (dest->uri) {
        dest->uri = zend_stat_string_copy(dest->uri);
    }
}

static zend_always_inline void zend_stat_request_release(zend_stat_request_t *request) {
    if (request->method) {
        zend_stat_string_release(request->method);
    }

    if (request->uri) {
        zend_stat_string_release(request->uri);
    }

    memset(request, 0, sizeof(zend_stat_request_t));
}
#endif	/* ZEND_STAT_REQUEST_H */
