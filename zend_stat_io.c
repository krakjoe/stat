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

#ifndef ZEND_STAT_IO
# define ZEND_STAT_IO

#include "zend_stat.h"
#include "zend_stat_buffer.h"
#include "zend_stat_io.h"

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

static struct {
    zend_stat_io_type_t     type;
    int                     descriptor;
    struct sockaddr         *address;
    zend_bool               closed;
    pthread_t               thread;
    zend_stat_buffer_t      *buffer;
} zend_stat_io;

#define ZTIO(v) zend_stat_io.v

#define ZEND_STAT_IO_SIZE(t) ((t == ZEND_STAT_IO_UNIX) ? sizeof(struct sockaddr_un) : sizeof(struct sockaddr_in))

static void* zend_stat_io_routine(void *arg) {
    struct sockaddr* address =
        (struct sockaddr*)
            pemalloc(ZEND_STAT_IO_SIZE(ZTIO(type)), 1);
    socklen_t length = ZEND_STAT_IO_SIZE(ZTIO(type));

    do {
        int client;

        memset(
            address, 0,
            ZEND_STAT_IO_SIZE(ZTIO(type)));

        client = accept(ZTIO(descriptor), address, &length);

        if (UNEXPECTED(FAILURE == client)) {
            if (ECONNABORTED == errno ||
                EINTR == errno) {
                continue;
            }

            break;
        }

        while (zend_stat_buffer_dump(ZTIO(buffer), client)) {
            if (__atomic_load_n(&ZTIO(closed), __ATOMIC_SEQ_CST)) {
                if (zend_stat_buffer_empty(ZTIO(buffer))) {
                    break;
                }
            }
        }

        close(client);
    } while (!__atomic_load_n(&ZTIO(closed), __ATOMIC_SEQ_CST));

    pefree(address, 1);

    pthread_exit(NULL);
}

zend_stat_io_type_t zend_stat_io_setup(char *uri, struct sockaddr **sa, int *so) {
    zend_stat_io_type_t type = ZEND_STAT_IO_UNKNOWN;
    char *buffer,
         *address =
            buffer = strdup(uri);
    size_t length = strlen(address);
    char *port = NULL;

    if ((length >= sizeof("unix://")-1) && (SUCCESS == memcmp(address, "unix://", sizeof("unix://")-1))) {
        type = ZEND_STAT_IO_UNIX;
        address += sizeof("unix://")-1;
        length -= sizeof("unix://")-1;
    } else if ((length >= sizeof("tcp://")-1) && (SUCCESS == memcmp(address, "tcp://", sizeof("tcp://")-1))) {
        type = ZEND_STAT_IO_TCP;
        address += sizeof("tcp://")-1;
        length -= sizeof("tcp://")-1;

        port = strrchr(address, ':');

        if (NULL == port) {
            type = ZEND_STAT_IO_UNKNOWN;
        } else {
            address[port - address] = 0;

            port++;
        }
    } else {
        type = ZEND_STAT_IO_UNIX;
    }

    switch (type) {
        case ZEND_STAT_IO_UNIX: {
            int try;
            struct sockaddr_un *un =
                (struct sockaddr_un*)
                    pecalloc(1, sizeof(struct sockaddr_un), 1);

            un->sun_family = AF_UNIX;

            try = socket(un->sun_family, SOCK_STREAM, 0);

            if (try == -1) {
                zend_error(E_WARNING,
                    "[STAT] %s - cannot create socket for %s",
                    strerror(errno),
                    uri);
                type = ZEND_STAT_IO_FAILED;
                pefree(un, 1);

                break;
            }

            strcpy(un->sun_path, address);

            unlink(un->sun_path);

            if (bind(try, (struct sockaddr*) un, sizeof(struct sockaddr_un)) == SUCCESS) {
                *so = try;
                *sa = (struct sockaddr*) un;

                free(buffer);
                return type;
            }

            zend_error(E_WARNING,
                "[STAT] %s - cannot create socket for %s",
                strerror(errno),
                uri);
            type = ZEND_STAT_IO_FAILED;
            close(try);
            free(un);
        } break;

        case ZEND_STAT_IO_TCP: {
            struct addrinfo *ai, *rp, hi;
            int gai_errno;

            memset(&hi, 0, sizeof(struct addrinfo));

            hi.ai_family = AF_UNSPEC;
            hi.ai_socktype = SOCK_STREAM;
            hi.ai_flags = AI_PASSIVE;
            hi.ai_protocol = IPPROTO_TCP;

            gai_errno = getaddrinfo(address, port, &hi, &ai);

            if (gai_errno != SUCCESS) {
                zend_error(E_WARNING,
                    "[STAT] %s - failed to get address for %s",
                    gai_strerror(gai_errno),
                    uri);
                type = ZEND_STAT_IO_FAILED;

                break;
            }

            for (rp = ai; rp != NULL; rp = rp->ai_next) {
                int try = socket(
                            rp->ai_family, rp->ai_socktype, rp->ai_protocol);

                if (try == -1) {
                    continue;
                }

#ifdef SO_REUSEADDR
                {
                    int option = 1;

                    setsockopt(
                        try,
                        SOL_SOCKET, SO_REUSEADDR,
                        (void*) &option, sizeof(int));
                }
#endif

                if (bind(try, rp->ai_addr, rp->ai_addrlen) == SUCCESS) {
                    *so = try;

                    freeaddrinfo(ai);
                    free(buffer);
                    return type;
                }

                close(try);
            }

            zend_error(E_WARNING,
                "[STAT] %s - cannot create socket for %s",
                strerror(errno),
                uri);
            type = ZEND_STAT_IO_FAILED;
            freeaddrinfo(ai);
        } break;

        case ZEND_STAT_IO_UNKNOWN:
            zend_error(E_WARNING,
                "[STAT] Failed to setup socket, %s is a malformed uri",
                uri);
        break;
    }

    free(buffer);
    return type;
}

zend_bool zend_stat_io_startup(char *uri, zend_stat_buffer_t *buffer)
{
    if (!uri) {
        return 1;
    }

    memset(&zend_stat_io, 0, sizeof(zend_stat_io));

    switch (ZTIO(type) = zend_stat_io_setup(uri, &ZTIO(address), &ZTIO(descriptor))) {
        case ZEND_STAT_IO_UNKNOWN:
        case ZEND_STAT_IO_FAILED:
            return 0;

        case ZEND_STAT_IO_UNIX:
        case ZEND_STAT_IO_TCP:
            /* all good */
        break;
    }

    if (listen(ZTIO(descriptor), 256) != SUCCESS) {
        zend_error(E_WARNING,
            "[STAT] %s - cannot listen on %s, ",
            strerror(errno), uri);
        zend_stat_io_shutdown();
        return 0;
    }

    ZTIO(buffer) = buffer;

    if (pthread_create(&ZTIO(thread), NULL, zend_stat_io_routine, NULL) != SUCCESS) {
        zend_error(E_WARNING,
            "[STAT] %s - cannot create thread for io on %s",
            strerror(errno), uri);
        zend_stat_io_shutdown();
        return 0;
    }

    return 1;
}

zend_bool zend_stat_io_write(int fd, char *message, size_t length) {
    ssize_t total = 0,
            bytes = 0;

    do {
        bytes = write(fd, message + total, length - total);

        if (bytes <= 0) {
            if (errno == EINTR) {
                continue;
            }

            return 0;
        }

        total += bytes;
    } while (total < length);

    return 1;
}

zend_bool zend_stat_io_write_string(int fd, zend_stat_string_t *string) {
    return zend_stat_io_write(fd, string->value, string->length);
}

zend_bool zend_stat_io_write_int(int fd, zend_long num) {
    char intbuf[128];

    sprintf(
        intbuf, ZEND_LONG_FMT, num);

    return zend_stat_io_write(fd, intbuf, strlen(intbuf));
}

zend_bool zend_stat_io_write_double(int fd, double num) {
    char dblbuf[128];

    sprintf(
        dblbuf, "%.10f", num);

    return zend_stat_io_write(fd, dblbuf, strlen(dblbuf));
}

void zend_stat_io_shutdown(void)
{
    if (!ZTIO(descriptor)) {
        return;
    }

    if (ZTIO(type) == ZEND_STAT_IO_UNIX) {
        struct sockaddr_un *un =
            (struct sockaddr_un*) ZTIO(address);

        unlink(un->sun_path);
        pefree(un, 1);
    }

    close(ZTIO(descriptor));

    __atomic_store_n(
        &ZTIO(closed), 1, __ATOMIC_SEQ_CST);

    pthread_join(ZTIO(thread), NULL);
}

#endif	/* ZEND_STAT_IO */
