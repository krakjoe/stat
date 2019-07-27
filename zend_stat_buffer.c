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

#ifndef ZEND_STAT_BUFFER
# define ZEND_STAT_BUFFER

#include "zend_stat.h"
#include "zend_stat_buffer.h"
#include "zend_stat_io.h"

struct _zend_stat_buffer_t {
    zend_stat_sample_t *samples;
    zend_stat_sample_t *position;
    zend_stat_sample_t *it;
    zend_stat_sample_t *end;
    zend_ulong max;
    zend_ulong used;
    zend_ulong size;
};

zend_stat_buffer_t* zend_stat_buffer_startup(zend_long samples) {
    size_t size = sizeof(zend_stat_buffer_t) +
                  (samples * sizeof(zend_stat_sample_t));
    zend_stat_buffer_t *buffer = zend_stat_map(size);

    if (!buffer) {
        zend_error(E_WARNING,
            "[STAT] Failed to allocate shared memory for buffer");
        return NULL;
    }

    memset(buffer, 0, size);

    buffer->samples =
        buffer->it =
        buffer->position =
            (zend_stat_sample_t*) (((char*) buffer) + sizeof(zend_stat_buffer_t));
    buffer->max       = samples;
    buffer->used      = 0;
    buffer->end       = buffer->position + buffer->max;
    buffer->size      = size;

    memset(buffer->samples, 0, sizeof(zend_stat_sample_t) * buffer->max);

    return buffer;
}

zend_bool zend_stat_buffer_empty(zend_stat_buffer_t *buffer) {
    return 0 == __atomic_load_n(&buffer->used, __ATOMIC_SEQ_CST);
}

void zend_stat_buffer_insert(zend_stat_buffer_t *buffer, zend_stat_sample_t *input) {
    zend_stat_sample_t *sample;
    zend_bool _unused = 0,
              _used   = 1;

    do {
        zend_bool _unbusy = 0,
                  _busy   = 1;

        sample = __atomic_fetch_add(
                    &buffer->position, sizeof(zend_stat_sample_t), __ATOMIC_SEQ_CST);

        if (UNEXPECTED(sample >= buffer->end)) {
            __atomic_store_n(
                &buffer->position,
                buffer->samples, __ATOMIC_SEQ_CST);
            continue;
        }

        if (UNEXPECTED(!__atomic_compare_exchange(
                &sample->state.busy,
                &_unbusy, &_busy,
                0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))) {
            continue;
        }

        break;
    } while(1);

    memcpy(
        ZEND_STAT_SAMPLE_DATA(sample),
        ZEND_STAT_SAMPLE_DATA(input),
        ZEND_STAT_SAMPLE_DATA_SIZE);

    if (__atomic_compare_exchange(&sample->state.used,
            &_unused, &_used, 0,
            __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        __atomic_fetch_add(&buffer->used, 1, __ATOMIC_SEQ_CST);
    }

    __atomic_store_n(&sample->state.busy, 0, __ATOMIC_SEQ_CST);
}

zend_bool zend_stat_buffer_dump(zend_stat_buffer_t *buffer, int fd) {
    zend_stat_sample_t *sample;
    zend_ulong tried = 0;

    if (zend_stat_buffer_empty(buffer)) {
        return 1;
    }

    while (tried++ < buffer->max) {
        zend_stat_sample_t sampled = zend_stat_sample_empty;
        zend_bool _unbusy = 0,
                  _busy   = 1,
                  _unused = 0,
                  _used = 1;

        sample = __atomic_fetch_add(
                   &buffer->it, sizeof(zend_stat_sample_t), __ATOMIC_SEQ_CST);

        if (UNEXPECTED(sample >= buffer->end)) {
            __atomic_store_n(
                &buffer->it,
                buffer->samples, __ATOMIC_SEQ_CST);
            continue;
        }

        if (UNEXPECTED(!__atomic_compare_exchange(
                &sample->state.busy,
                &_unbusy, &_busy,
                0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))) {
            continue;
        }

        if (EXPECTED(__atomic_compare_exchange(
                &sample->state.used,
                &_used, &_unused,
                0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))) {

            __atomic_sub_fetch(&buffer->used, 1, __ATOMIC_SEQ_CST);

            memcpy(&sampled, sample, sizeof(zend_stat_sample_t));
        }

        __atomic_store_n(&sample->state.busy, 0, __ATOMIC_SEQ_CST);

        if (UNEXPECTED(ZEND_STAT_SAMPLE_UNUSED == sampled.type)) {
            continue;
        }

        zend_stat_io_write_literal_ex(fd, "{", return 0);

        zend_stat_io_write_literal_ex(fd, "\"pid\": ", return 0);
        zend_stat_io_write_int_ex(fd, sampled.pid, return 0);
        zend_stat_io_write_literal_ex(fd, ", ", return 0);

        zend_stat_io_write_literal_ex(fd, "\"elapsed\": ", return 0);
        zend_stat_io_write_double_ex(fd, sampled.elapsed, return 0);
        zend_stat_io_write_literal_ex(fd, ", ", return 0);

        zend_stat_io_write_literal_ex(fd, "\"memory\": {", return 0);

        zend_stat_io_write_literal_ex(fd, "\"used\": ", return 0);
        zend_stat_io_write_int_ex(fd, sampled.memory.used, return 0);
        zend_stat_io_write_literal_ex(fd, ", \"peak\": ", return 0);
        zend_stat_io_write_int_ex(fd, sampled.memory.peak, return 0);

        zend_stat_io_write_literal_ex(fd, "}", return 0);

        if (sampled.type == ZEND_STAT_SAMPLE_MEMORY) {
            zend_stat_io_write_literal_ex(fd, "}\n", return 0);
            continue;
        }

        if (sampled.location.file) {
            zend_stat_io_write_literal_ex(fd, ", \"location\": {", return 0);

            zend_stat_io_write_literal_ex(fd, "\"file\": \"", return 0);
            zend_stat_io_write_string_ex(fd, sampled.location.file, return 0);
            zend_stat_io_write_literal_ex(fd, "\"", return 0);

            if (sampled.location.line) {
                zend_stat_io_write_literal_ex(fd, ", \"line\": ", return 0);
                zend_stat_io_write_int_ex(fd, sampled.location.line, return 0);
            }

            zend_stat_io_write_literal_ex(fd, ", \"offset\": ", return 0);
            zend_stat_io_write_int_ex(fd, sampled.location.offset, return 0);

            zend_stat_io_write_literal_ex(fd, ", \"opcode\": \"", return 0);
            zend_stat_io_write_string_ex(fd,
                zend_stat_string_opcode(sampled.location.opcode), return 0);
            zend_stat_io_write_literal_ex(fd, "\"", return 0);

            zend_stat_io_write_literal_ex(fd, "}", return 0);
        }

        if (sampled.symbol.scope || sampled.symbol.function) {
            zend_stat_io_write_literal_ex(fd, ", \"symbol\": {", return 0);

            if (sampled.symbol.scope) {
                zend_stat_io_write_literal_ex(fd, "\"scope\": \"", return 0);
                zend_stat_io_write_string_ex(fd, sampled.symbol.scope, return 0);
                zend_stat_io_write_literal_ex(fd, "\", ", return 0);
            }

            if (sampled.symbol.function) {
                zend_stat_io_write_literal_ex(fd, "\"function\": \"", return 0);
                zend_stat_io_write_string_ex(fd, sampled.symbol.function, return 0);
                zend_stat_io_write_literal_ex(fd, "\"", return 0);
            }

            zend_stat_io_write_literal_ex(fd, "}", return 0);
        }

        if (sampled.arginfo.length) {
            zval *it = sampled.arginfo.info,
                 *end = it + sampled.arginfo.length;

            zend_stat_io_write_literal_ex(fd, ", \"arginfo\": [", return 0);

            while (it < end) {
                zend_stat_io_write_literal_ex(fd, "\"", return 0);
                switch (Z_TYPE_P(it)) {
                    case IS_UNDEF:
                    case IS_NULL:
                        zend_stat_io_write_literal_ex(fd, "null", return 0);
                    break;

                    case IS_REFERENCE:
                        zend_stat_io_write_literal_ex(fd, "reference", return 0);
                    break;

                    case IS_DOUBLE:
                        zend_stat_io_write_literal_ex(fd, "float(", return 0);
                        zend_stat_io_write_double_ex(fd, Z_DVAL_P(it), return 0);
                        zend_stat_io_write_literal_ex(fd, ")", return 0);
                    break;

                    case IS_LONG:
                        zend_stat_io_write_literal_ex(fd, "int(", return 0);
                        zend_stat_io_write_int_ex(fd, Z_LVAL_P(it), return 0);
                        zend_stat_io_write_literal_ex(fd, ")", return 0);
                    break;

                    case IS_TRUE:
                    case IS_FALSE:
                        zend_stat_io_write_literal_ex(fd, "bool(", return 0);
                        zend_stat_io_write_int_ex(fd, zend_is_true(it), return 0);
                        zend_stat_io_write_literal_ex(fd, ")", return 0);
                    break;

                    case IS_STRING:
                        zend_stat_io_write_literal_ex(fd, "string", return 0);
                    break;

                    case IS_OBJECT:
                        zend_stat_io_write_literal_ex(fd, "object", return 0);
                    break;

                    case IS_RESOURCE:
                        zend_stat_io_write_literal_ex(fd, "resource", return 0);
                    break;

                    default: {
                        const char *type = zend_get_type_by_const(Z_TYPE_P(it));

                        if (EXPECTED(type)) {
                            zend_stat_io_write_ex(fd,
                                (char*) type, strlen(type), return 0);
                        }
                    }
                }
                zend_stat_io_write_literal_ex(fd, "\"", return 0);
                it++;

                if (it < end) {
                    zend_stat_io_write_literal_ex(fd, ",", return 0);
                }
            }
            zend_stat_io_write_literal_ex(fd, "]", return 0);
        }

        zend_stat_io_write_literal_ex(fd, "}\n", return 0);
    }

    return 1;
}

void zend_stat_buffer_shutdown(zend_stat_buffer_t *buffer) {
    zend_stat_unmap(buffer, buffer->size);
}

#endif	/* ZEND_STAT_BUFFER */
