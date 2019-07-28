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

#ifndef ZEND_STAT_CONTROL
# define ZEND_STAT_CONTROL

#include "zend_stat.h"
#include "zend_stat_io.h"
#include "zend_stat_control.h"

typedef enum {
    ZEND_STAT_CONTROL_UNKNOWN  = 0,
    ZEND_STAT_CONTROL_FAILED   = (1<<0),
    ZEND_STAT_CONTROL_INTERVAL = (1<<1),
    ZEND_STAT_CONTROL_ARGINFO  = (1<<2)
} zend_stat_control_type_t;

typedef struct _zend_stat_control_t {
    int64_t type;
    int64_t param;
} zend_stat_control_t;

const static zend_stat_control_t
    zend_stat_control_empty =
        {ZEND_STAT_CONTROL_UNKNOWN, 0};

static zend_stat_control_type_t zend_stat_control_read(int client, int64_t *param) {
    zend_stat_control_t control = zend_stat_control_empty;
    ssize_t read = recv(
        client,
        &control,
        sizeof(zend_stat_control_t), MSG_WAITALL);

    if (read != sizeof(zend_stat_control_t)) {
        if (errno == EINTR) {
            return zend_stat_control_read(client, param);
        }

        return ZEND_STAT_CONTROL_FAILED;
    }

    *param = control.param;

    return control.type;
}

static void zend_stat_control(zend_stat_io_t *io, int client) {
    while (!zend_stat_io_closed(io)) {
        int64_t param = 0;

        switch (zend_stat_control_read(client, &param)) {
            case ZEND_STAT_CONTROL_INTERVAL:
                if (param > 0) {
                    zend_stat_buffer_interval_set(io->buffer, param);
                }
            break;

            case ZEND_STAT_CONTROL_ARGINFO:
                zend_stat_buffer_arginfo_set(io->buffer, (zend_bool) param);
            break;

            case ZEND_STAT_CONTROL_FAILED:
                return;
        }
    }
}

zend_bool zend_stat_control_startup(zend_stat_io_t *io, zend_stat_buffer_t *buffer, char *control) {
    return zend_stat_io_startup(io, control, buffer, zend_stat_control);
}

void zend_stat_control_shutdown(zend_stat_io_t *io) {
    zend_stat_io_shutdown(io);
}
#endif
