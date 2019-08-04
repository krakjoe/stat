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

#ifndef ZEND_STAT_REQUEST
# define ZEND_STAT_REQUEST

#include "zend_stat.h"
#include "zend_stat_request.h"

#include "SAPI.h"

zend_bool zend_stat_request_create(zend_stat_request_t *request) {
    sapi_request_info *ri = &SG(request_info);

    memset(request, 0, sizeof(zend_stat_request_t));

    request->pid     = zend_stat_pid();
    request->elapsed = zend_stat_time();

    if (EXPECTED(ri->path_translated)) {
        request->path =
            zend_stat_string_temporary(
                ri->path_translated, strlen(ri->path_translated));

        if (UNEXPECTED(NULL == request->path)) {
            zend_stat_request_release(request);

            return 0;
        }
    }

    if (EXPECTED(ri->request_method)) {
        request->method =
            zend_stat_string_temporary(
                ri->request_method, strlen(ri->request_method));

        if (UNEXPECTED(NULL == request->method)) {
            zend_stat_request_release(request);

            return 0;
        }
    }

    if (EXPECTED(ri->request_uri)) {
        request->uri =
            zend_stat_string_temporary(
                ri->request_uri, strlen(ri->request_uri));

        if (UNEXPECTED(NULL == request->uri)) {
            zend_stat_request_release(request);

            return 0;
        }
    }

    /* anything else ? */

    return 1;
}
#endif	/* ZEND_STAT_REQUEST */
