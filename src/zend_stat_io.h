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

#ifndef ZEND_STAT_IO_H
# define ZEND_STAT_IO_H

#include "zend_stat_buffer.h"
#include "zend_stat_strings.h"

#include <pthread.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>

typedef enum {
    ZEND_STAT_IO_UNKNOWN,
    ZEND_STAT_IO_UNIX,
    ZEND_STAT_IO_TCP,
    ZEND_STAT_IO_FAILED
} zend_stat_io_type_t;

typedef struct _zend_stat_io_t zend_stat_io_t;

typedef void (zend_stat_io_routine_t) (zend_stat_io_t *io, int client);

struct _zend_stat_io_t {
    zend_stat_io_type_t     type;
    int                     descriptor;
    struct sockaddr         *address;
    zend_bool               closed;
    pthread_t               thread;
    zend_stat_buffer_t      *buffer;
    zend_stat_io_routine_t  *routine;
};

typedef struct _zend_stat_io_buffer_t {
    char *buf;
    zend_long size;
    zend_long used;
} zend_stat_io_buffer_t;

zend_bool zend_stat_io_buffer_alloc(zend_stat_io_buffer_t *buffer, zend_long size);
zend_bool zend_stat_io_buffer_append(zend_stat_io_buffer_t *buffer, const char *bytes, zend_long size);
zend_bool zend_stat_io_buffer_appends(zend_stat_io_buffer_t *buffer, zend_stat_string_t *string);
zend_bool zend_stat_io_buffer_appendf(zend_stat_io_buffer_t *buffer, char *format, ...);
zend_bool zend_stat_io_buffer_flush(zend_stat_io_buffer_t *buffer, int fd);
void zend_stat_io_buffer_free(zend_stat_io_buffer_t *buffer);

zend_bool zend_stat_io_startup(zend_stat_io_t *io, char *uri, zend_stat_buffer_t *buffer, zend_stat_io_routine_t *routine);
zend_bool zend_stat_io_closed(zend_stat_io_t *io);
void zend_stat_io_shutdown(zend_stat_io_t *io);

zend_bool zend_stat_io_write(int fd, char *message, size_t length);
#endif	/* ZEND_STAT_IO_H */
