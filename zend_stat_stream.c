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

static void* zend_stat_stream_routine(zend_stat_io_t *io) {
    struct sockaddr* address =
        (struct sockaddr*)
            pemalloc(ZEND_STAT_IO_SIZE(io->type), 1);
    socklen_t length = ZEND_STAT_IO_SIZE(io->type);

    do {
        int client;

        memset(
            address, 0,
            ZEND_STAT_IO_SIZE(io->type));

        client = accept(io->descriptor, address, &length);

        if (UNEXPECTED(FAILURE == client)) {
            if (ECONNABORTED == errno ||
                EINTR == errno) {
                continue;
            }

            break;
        }

        while (zend_stat_buffer_dump(io->buffer, client)) {
            if (__atomic_load_n(&io->closed, __ATOMIC_SEQ_CST)) {
                if (zend_stat_buffer_empty(io->buffer)) {
                    break;
                }
            }
        }

        close(client);
    } while (!__atomic_load_n(&io->closed, __ATOMIC_SEQ_CST));

    pefree(address, 1);

    pthread_exit(NULL);
}

zend_bool zend_stat_stream_startup(zend_stat_io_t *io, zend_stat_buffer_t *buffer, char *stream) {
    if (!stream) {
        return 1;
    }

    memset(io, 0, sizeof(zend_stat_io_t));

    switch (io->type = zend_stat_io_socket(stream, &io->address, &io->descriptor)) {
        case ZEND_STAT_IO_UNKNOWN:
        case ZEND_STAT_IO_FAILED:
            return 0;

        case ZEND_STAT_IO_UNIX:
        case ZEND_STAT_IO_TCP:
            /* all good */
        break;
    }

    if (listen(io->descriptor, 256) != SUCCESS) {
        zend_error(E_WARNING,
            "[STAT] %s - cannot listen on %s, ",
            strerror(errno), stream);
        zend_stat_stream_shutdown(io);
        return 0;
    }

    io->buffer = buffer;

    if (pthread_create(&io->thread, 
            NULL,
            (void*)(void*)
                zend_stat_stream_routine,
            (void*) io) != SUCCESS) {
        zend_error(E_WARNING,
            "[STAT] %s - cannot create thread for io on %s",
            strerror(errno), stream);
        zend_stat_stream_shutdown(io);
        return 0;
    }

    return 1;
}

void zend_stat_stream_shutdown(zend_stat_io_t *io) {
    if (!io->descriptor) {
        return;
    }

    if (io->type == ZEND_STAT_IO_UNIX) {
        struct sockaddr_un *un =
            (struct sockaddr_un*) io->address;

        unlink(un->sun_path);
        pefree(un, 1);
    }

    shutdown(io->descriptor, SHUT_RD);
    close(io->descriptor);

    __atomic_store_n(
        &io->closed, 1, __ATOMIC_SEQ_CST);

    pthread_join(io->thread, NULL);
}
#endif
