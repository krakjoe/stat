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

#ifndef ZEND_STAT_SAMPLE
# define ZEND_STAT_SAMPLE

#include "zend_stat.h"
#include "zend_stat_io.h"
#include "zend_stat_sample.h"

static zend_bool zend_stat_sample_write_type(int fd, zend_uchar type) {
    zend_stat_io_write_literal_ex(fd, "\"type\": ", return 0);
    switch (type) {
        case ZEND_STAT_SAMPLE_MEMORY:
            zend_stat_io_write_literal_ex(fd, "\"memory\"", return 0);
        break;

        case ZEND_STAT_SAMPLE_INTERNAL:
            zend_stat_io_write_literal_ex(fd, "\"internal\"", return 0);
        break;

        case ZEND_STAT_SAMPLE_USER:
            zend_stat_io_write_literal_ex(fd, "\"user\"", return 0);
        break;
    }

    return 1;
}

static zend_bool zend_stat_sample_write_request(int fd, zend_stat_request_t *request) {
    zend_stat_io_write_literal_ex(fd, ", \"request\": {", return 0);
    zend_stat_io_write_literal_ex(fd, "\"pid\": ", return 0);
    zend_stat_io_write_int_ex(fd, request->pid, return 0);
    zend_stat_io_write_literal_ex(fd, ", \"elapsed\": ", return 0);
    zend_stat_io_write_double_ex(fd, request->elapsed, return 0);

    if (request->path) {
        zend_stat_io_write_literal_ex(fd, ", \"path\": \"", return 0);
        zend_stat_io_write_string_ex(fd, request->path, return 0);
        zend_stat_io_write_literal_ex(fd, "\"", return 0);
    }

    if (request->method) {
        if (request->path) {
            zend_stat_io_write_literal_ex(fd, ", ", return 0);
        }
        zend_stat_io_write_literal_ex(fd, "\"method\": \"", return 0);
        zend_stat_io_write_string_ex(fd, request->method, return 0);
        zend_stat_io_write_literal_ex(fd, "\"", return 0);
    }

    if (request->uri) {
        if (request->path || request->method) {
            zend_stat_io_write_literal_ex(fd, ", ", return 0);
        }
        zend_stat_io_write_literal_ex(fd, "\"uri\": \"", return 0);
        zend_stat_io_write_string_ex(fd, request->uri, return 0);
        zend_stat_io_write_literal_ex(fd, "\"", return 0);
    }

    zend_stat_io_write_literal_ex(fd, "}", return 0);

    return 1;
}

static zend_bool zend_stat_sample_write_memory(int fd, zend_stat_sample_memory_t *memory) {
    zend_stat_io_write_literal_ex(fd, ", \"memory\": {", return 0);

    zend_stat_io_write_literal_ex(fd, "\"used\": ", return 0);
    zend_stat_io_write_int_ex(fd, memory->used, return 0);
    zend_stat_io_write_literal_ex(fd, ", \"peak\": ", return 0);
    zend_stat_io_write_int_ex(fd, memory->peak, return 0);

    zend_stat_io_write_literal_ex(fd, "}", return 0);

    return 1;
}

static zend_bool zend_stat_sample_write_symbol(int fd, char *label, size_t size, zend_stat_sample_symbol_t *symbol) {
    if (!symbol->file &&
        !symbol->scope &&
        !symbol->function) {
        return 1;
    }

    zend_stat_io_write_literal_ex(fd, ", \"", return 0);
    zend_stat_io_write_ex(fd, label, size, return 0);
    zend_stat_io_write_literal_ex(fd, "\": {", return 0);

    if (symbol->file) {
        zend_stat_io_write_literal_ex(fd, "\"file\": \"", return 0);
        zend_stat_io_write_string_ex(fd, symbol->file, return 0);
        zend_stat_io_write_literal_ex(fd, "\"", return 0);
    }

    if (symbol->scope) {
        if (symbol->file) {
            zend_stat_io_write_literal_ex(fd, ", ", return 0);
        }
        zend_stat_io_write_literal_ex(fd, "\"scope\": \"", return 0);
        zend_stat_io_write_string_ex(fd, symbol->scope, return 0);
        zend_stat_io_write_literal_ex(fd, "\"", return 0);
    }

    if (symbol->function) {
        if (symbol->scope || symbol->file) {
            zend_stat_io_write_literal_ex(fd, ", ", return 0);
        }
        zend_stat_io_write_literal_ex(fd, "\"function\": \"", return 0);
        zend_stat_io_write_string_ex(fd, symbol->function, return 0);
        zend_stat_io_write_literal_ex(fd, "\"", return 0);
    }

    zend_stat_io_write_literal_ex(fd, "}", return 0);

    return 1;
}

static zend_bool zend_stat_sample_write_opline(int fd, zend_stat_sample_opline_t *opline) {
    if (!opline->line &&
        !opline->offset &&
        !opline->opcode) {
        return 1;
    }

    zend_stat_io_write_literal_ex(fd, ", \"opline\": {", return 0);

    if (opline->line) {
        zend_stat_io_write_literal_ex(fd, "\"line\": ", return 0);
        zend_stat_io_write_int_ex(fd, opline->line, return 0);
    }

    if (opline->offset) {
        if (opline->line) {
            zend_stat_io_write_literal_ex(fd, ", ", return 0);
        }
        zend_stat_io_write_literal_ex(fd, "\"offset\": ", return 0);
        zend_stat_io_write_int_ex(fd, opline->offset, return 0);
    }

    if ((opline->opcode > 0) &&
        (opline->opcode <= ZEND_VM_LAST_OPCODE)) {
        zend_stat_io_write_literal_ex(fd, ", \"opcode\": \"", return 0);
        zend_stat_io_write_string_ex(fd,
            zend_stat_string_opcode(opline->opcode), return 0);
        zend_stat_io_write_literal_ex(fd, "\"", return 0);
    }

    zend_stat_io_write_literal_ex(fd, "}", return 0);

    return 1;
}

zend_bool zend_stat_sample_write(zend_stat_sample_t *sample, int fd) {
    zend_stat_io_write_literal_ex(fd, "{", return 0);
    
    if (!zend_stat_sample_write_type(fd, sample->type)) {
        return 0;
    }

    if (!zend_stat_sample_write_request(fd, &sample->request)) {
        return 0;
    }

    zend_stat_io_write_literal_ex(fd, ", \"elapsed\": ", return 0);
    zend_stat_io_write_double_ex(fd, sample->elapsed, return 0);
    
    if (!zend_stat_sample_write_memory(fd, &sample->memory)) {
        return 0;
    }

    if (sample->type == ZEND_STAT_SAMPLE_MEMORY) {
        zend_stat_io_write_literal_ex(fd, "}\n", return 0);
        return 1;
    }

    if (!zend_stat_sample_write_symbol(fd, 
            "symbol", sizeof("symbol") -1, &sample->symbol)) {
        return 0;
    }

    if (sample->arginfo.length) {
        zval *it = sample->arginfo.info,
             *end = it + sample->arginfo.length;

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

    if (sample->type == ZEND_STAT_SAMPLE_USER) {
        if (!zend_stat_sample_write_opline(fd, &sample->location.opline)) {
            return 0;
        }
    } else {
        if (!zend_stat_sample_write_symbol(fd, 
                "caller", sizeof("caller")-1, &sample->location.caller)) {
            return 0;
        }
    }

    zend_stat_io_write_literal_ex(fd, "}\n", return 0);
    return 1;
}

#endif
