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

#ifndef ZEND_STAT_STREAM
# define ZEND_STAT_STREAM

#include "zend_stat.h"
#include "zend_stat_io.h"
#include "zend_stat_stream.h"

static zend_always_inline void zend_stat_stream_yield(zend_stat_io_t *io) {
    zend_long interval =
        zend_stat_buffer_interval_get(io->buffer) / 1000;

    usleep(ceil(interval / 2));
}

static void zend_stat_stream(zend_stat_io_t *io, int client) {
    while (zend_stat_buffer_dump(io->buffer, client)) {
        if (zend_stat_buffer_empty(io->buffer)) {
            if (zend_stat_io_closed(io)) {
                return;
            }

            zend_stat_stream_yield(io);
        }
    }
}

zend_bool zend_stat_stream_startup(zend_stat_io_t *io, zend_stat_buffer_t *buffer, char *stream) {
    return zend_stat_io_startup(io, stream, buffer, zend_stat_stream);
}

void zend_stat_stream_shutdown(zend_stat_io_t *io) {
    zend_stat_io_shutdown(io);
}
#endif
