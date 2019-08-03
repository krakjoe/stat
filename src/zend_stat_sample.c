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

static zend_always_inline const char* zend_stat_sample_type_name(zend_uchar type) {
    switch (type) {
        case ZEND_STAT_SAMPLE_MEMORY:
            return "memory";

        case ZEND_STAT_SAMPLE_INTERNAL:
            return "internal";

        case ZEND_STAT_SAMPLE_USER:
            return "user";
    }

    return "unknown";
}

static zend_bool zend_stat_sample_write_type(zend_stat_io_buffer_t *iob, zend_uchar type) {
    const char *name = zend_stat_sample_type_name(type);

    if (!zend_stat_io_buffer_append(iob, "\"type\": \"", sizeof("\"type\": \"")-1) ||
        !zend_stat_io_buffer_append(iob, name, strlen(name)) ||
        !zend_stat_io_buffer_append(iob, "\"", sizeof("\"")-1)) {
        return 0;
    }

    return 1;
}

static zend_bool zend_stat_sample_write_request(zend_stat_io_buffer_t *iob, zend_stat_request_t *request) {
    if (!zend_stat_io_buffer_appendf(iob,
            ", \"request\": {\"pid\": %d, \"elapsed\": %.10f",
            request->pid,
            request->elapsed)) {
        return 0;
    }

    if (request->path) {
        if (!zend_stat_io_buffer_append(iob, ", \"path\": \"", sizeof(", \"path\": \"")-1) ||
            !zend_stat_io_buffer_appends(iob, request->path) ||
            !zend_stat_io_buffer_append(iob, "\"", sizeof("\"")-1)) {
            return 0;
        }
    }

    if (request->method) {
        if (request->path) {
            if (!zend_stat_io_buffer_append(iob, ", ", sizeof(", ")-1)) {
                return 0;
            }
        }
        if (!zend_stat_io_buffer_append(iob, "\"method\": \"", sizeof("\"method\": \"")-1) ||
            !zend_stat_io_buffer_appends(iob, request->method) ||
            !zend_stat_io_buffer_append(iob, "\"", sizeof("\"")-1)) {
            return 0;
        }
    }

    if (request->uri) {
        if (request->path || request->method) {
            if (!zend_stat_io_buffer_append(iob, ", ", sizeof(", ")-1)) {
                return 0;
            }
        }
        if (!zend_stat_io_buffer_append(iob, "\"uri\": \"", sizeof("\"uri\": \"")-1) ||
            !zend_stat_io_buffer_appends(iob, request->uri) ||
            !zend_stat_io_buffer_append(iob, "\"", sizeof("\"")-1)) {
            return 0;
        }
    }

    if (!zend_stat_io_buffer_append(iob, "}", sizeof("}")-1)) {
        return 0;
    }

    return 1;
}

static zend_bool zend_stat_sample_write_memory(zend_stat_io_buffer_t *iob, zend_stat_sample_memory_t *memory) {
    if (!zend_stat_io_buffer_appendf(iob, ", \"memory\": {\"used\": %d, \"peak\": %d}", memory->used, memory->peak)) {
        return 0;
    }

    return 1;
}

static zend_bool zend_stat_sample_write_symbol(zend_stat_io_buffer_t *iob, char *label, zend_stat_sample_symbol_t *symbol) {
    if (!symbol->file &&
        !symbol->scope &&
        !symbol->function) {
        return 1;
    }

    if (!zend_stat_io_buffer_append(iob, ", \"", sizeof(", \"")-1) ||
        !zend_stat_io_buffer_append(iob, label, strlen(label)) ||
        !zend_stat_io_buffer_append(iob, "\": {", sizeof("\": {")-1)) {
        return 0;
    }

    if (symbol->file) {
        if (!zend_stat_io_buffer_append(iob, "\"file\": \"", sizeof("\"file\": \"")-1) ||
            !zend_stat_io_buffer_appends(iob, symbol->file) ||
            !zend_stat_io_buffer_append(iob, "\"", sizeof("\"")-1)) {
            return 0;
        }
    }

    if (symbol->scope) {
        if (symbol->file) {
            if (!zend_stat_io_buffer_append(iob, ", ", sizeof(", ")-1)) {
                return 0;
            }
        }
        if (!zend_stat_io_buffer_append(iob, "\"scope\": \"", sizeof("\"scope\": \"")-1) ||
            !zend_stat_io_buffer_appends(iob, symbol->scope) ||
            !zend_stat_io_buffer_append(iob, "\"", sizeof("\"")-1)) {
            return 0;
        }
    }

    if (symbol->function) {
        if (symbol->file || symbol->scope) {
            if (!zend_stat_io_buffer_append(iob, ", ", sizeof(", ")-1)) {
                return 0;
            }
        }

        if (!zend_stat_io_buffer_append(iob, "\"function\": \"", sizeof("\"function\": \"")-1) ||
            !zend_stat_io_buffer_appends(iob, symbol->function) ||
            !zend_stat_io_buffer_append(iob, "\"", sizeof("\"")-1)) {
            return 0;
        }
    }

    if (!zend_stat_io_buffer_append(iob, "}", sizeof("}")-1)) {
        return 0;
    }

    return 1;
}

static zend_bool zend_stat_sample_write_opline(zend_stat_io_buffer_t *iob, zend_stat_sample_opline_t *opline) {
    if (!opline->line &&
        !opline->offset &&
        !opline->opcode) {
        return 1;
    }

    if (!zend_stat_io_buffer_append(iob, ", \"opline\": {", sizeof(", \"opline\": {")-1)) {
        return 0;
    }

    if (opline->line) {
        if (!zend_stat_io_buffer_appendf(iob, "\"line\": %d", opline->line)) {
            return 0;
        }
    }

    if (opline->offset) {
        if (opline->line) {
            if (!zend_stat_io_buffer_append(iob, ", ", sizeof(", ")-1)) {
                return 0;
            }
        }

        if (!zend_stat_io_buffer_appendf(iob, "\"offset\": %d", opline->offset)) {
            return 0;
        }
    }

    if ((opline->opcode > 0) &&
        (opline->opcode <= ZEND_VM_LAST_OPCODE)) {
        if (opline->line || opline->offset) {
            if (!zend_stat_io_buffer_append(iob, ", ", sizeof(", ")-1)) {
                return 0;
            }
        }

        if (!zend_stat_io_buffer_append(iob, "\"opcode\": \"", sizeof("\"opcode\": \"")-1) ||
            !zend_stat_io_buffer_appends(iob, zend_stat_string_opcode(opline->opcode)) ||
            !zend_stat_io_buffer_append(iob, "\"", sizeof("\"")-1)) {
            return 0;
        }
    }

    if (!zend_stat_io_buffer_append(iob, "}", sizeof("}")-1)) {
        return 0;
    }

    return 1;
}

zend_bool zend_stat_sample_write_arginfo(zend_stat_io_buffer_t *iob, zend_stat_sample_arginfo_t *arginfo) {
    zval *it = arginfo->info,
         *end = it + arginfo->length;
    if (0 == arginfo->length) {
        return 1;
    }

    if (!zend_stat_io_buffer_append(iob, ", \"arginfo\": [", sizeof(", \"arginfo\": [")-1)) {
        return 0;
    }

    while (it < end) {
        if (!zend_stat_io_buffer_append(iob, "\"", sizeof("\"")-1)) {
            return 0;
        }

        switch (Z_TYPE_P(it)) {
            case IS_UNDEF:
            case IS_NULL:
                if (!zend_stat_io_buffer_append(iob, "null", sizeof("null")-1)) {
                    return 0;
                }
            break;

            case IS_REFERENCE:
                if (!zend_stat_io_buffer_append(iob, "reference", sizeof("reference")-1)) {
                    return 0;
                }
            break;

            case IS_DOUBLE:
                if (!zend_stat_io_buffer_appendf(iob, "float(%.10f)", Z_DVAL_P(it))) {
                    return 0;
                }
            break;

            case IS_LONG:
                if (!zend_stat_io_buffer_appendf(iob, "int(%.10f)", Z_LVAL_P(it))) {
                    return 0;
                }
            break;

            case IS_TRUE:
            case IS_FALSE:
                if (!zend_stat_io_buffer_appendf(iob, "bool(%s)", zend_is_true(it) ? "true" : "false")) {
                    return 0;
                }
            break;

            case IS_STRING:
                if (!zend_stat_io_buffer_append(iob, "string", sizeof("string")-1)) {
                    return 0;
                }
            break;

            case IS_OBJECT:
                if (!zend_stat_io_buffer_append(iob, "object", sizeof("object")-1)) {
                    return 0;
                }
            break;

            case IS_RESOURCE:
                if (!zend_stat_io_buffer_append(iob, "resource", sizeof("resource")-1)) {
                    return 0;
                }
            break;

            default: {
                const char *type = zend_get_type_by_const(Z_TYPE_P(it));

                if (EXPECTED(type)) {
                    if (!zend_stat_io_buffer_append(iob, type, strlen(type))) {
                        return 0;
                    }
                }
            }
        }
        if (!zend_stat_io_buffer_append(iob, "\"", sizeof("\"")-1)) {
            return 0;
        }
        it++;

        if (it < end) {
            if (!zend_stat_io_buffer_append(iob, ",", sizeof(",")-1)) {
                return 0;
            }
        }
    }

    if (!zend_stat_io_buffer_append(iob, "]", sizeof("]")-1)) {
        return 0;
    }

    return 1;
}

zend_bool zend_stat_sample_write(zend_stat_sample_t *sample, int fd) {
    zend_stat_io_buffer_t iob;

    if (!zend_stat_io_buffer_alloc(&iob, 8192)) {
        goto _zend_stat_sample_write_abort;
    }

    if (!zend_stat_io_buffer_append(&iob, "{", sizeof("{")-1)) {
        goto _zend_stat_sample_write_abort;
    }

    if (!zend_stat_sample_write_type(&iob, sample->type)) {
        goto _zend_stat_sample_write_abort;
    }

    if (!zend_stat_sample_write_request(&iob, &sample->request)) {
        goto _zend_stat_sample_write_abort;
    }

    if (!zend_stat_io_buffer_appendf(&iob, ", \"elapsed\": %.10f", sample->elapsed)) {
        goto _zend_stat_sample_write_abort;
    }

    if (!zend_stat_sample_write_memory(&iob, &sample->memory)) {
        goto _zend_stat_sample_write_abort;
    }

    if (sample->type == ZEND_STAT_SAMPLE_MEMORY) {
        if (!zend_stat_io_buffer_append(&iob, "}\n", sizeof("}\n")-1)) {
            goto _zend_stat_sample_write_abort;
        }
        goto _zend_stat_sample_write_flush;
    }

    if (!zend_stat_sample_write_symbol(&iob, "symbol", &sample->symbol)) {
        goto _zend_stat_sample_write_abort;
    }

    if (!zend_stat_sample_write_arginfo(&iob, &sample->arginfo)) {
        goto _zend_stat_sample_write_abort;
    }

    if (sample->type == ZEND_STAT_SAMPLE_USER) {
        if (!zend_stat_sample_write_opline(&iob, &sample->location.opline)) {
            goto _zend_stat_sample_write_abort;
        }
    } else {
        if (!zend_stat_sample_write_symbol(&iob, "caller", &sample->location.caller)) {
            goto _zend_stat_sample_write_abort;
        }
    }

    if (!zend_stat_io_buffer_append(&iob, "}\n", sizeof("}\n")-1)) {
        goto _zend_stat_sample_write_abort;
    }

_zend_stat_sample_write_flush:
    return zend_stat_io_buffer_flush(&iob, fd);

_zend_stat_sample_write_abort:
    zend_stat_io_buffer_free(&iob);
    return 0;
}

#endif
