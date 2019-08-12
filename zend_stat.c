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

#ifndef ZEND_STAT
# define ZEND_STAT

#define ZEND_STAT_EXTNAME   "Stat"
#define ZEND_STAT_VERSION   "0.0.1-dev"
#define ZEND_STAT_AUTHOR    "krakjoe"
#define ZEND_STAT_URL       "https://github.com/krakjoe/stat"
#define ZEND_STAT_COPYRIGHT "Copyright (c) 2019"

#if defined(__GNUC__) && __GNUC__ >= 4
# define ZEND_STAT_EXTENSION_API __attribute__ ((visibility("default")))
#else
# define ZEND_STAT_EXTENSION_API
#endif

#include "zend_stat.h"
#include "zend_stat_arena.h"
#include "zend_stat_buffer.h"
#include "zend_stat_control.h"
#include "zend_stat_ini.h"
#include "zend_stat_io.h"
#include "zend_stat_request.h"
#include "zend_stat_sampler.h"
#include "zend_stat_stream.h"
#include "zend_stat_strings.h"

static pid_t                   zend_stat_main = 0;
static zend_stat_buffer_t*     zend_stat_buffer = NULL;
static zend_stat_io_t          zend_stat_stream;
static zend_stat_io_t          zend_stat_control;
static double                  zend_stat_started = 0;

static int  zend_stat_startup(zend_extension*);
static void zend_stat_shutdown(zend_extension *);
static void zend_stat_activate(void);
static void zend_stat_deactivate(void);

ZEND_STAT_EXTENSION_API zend_extension_version_info extension_version_info = {
    ZEND_EXTENSION_API_NO,
    ZEND_EXTENSION_BUILD_ID
};

ZEND_STAT_EXTENSION_API zend_extension zend_extension_entry = {
    ZEND_STAT_EXTNAME,
    ZEND_STAT_VERSION,
    ZEND_STAT_AUTHOR,
    ZEND_STAT_URL,
    ZEND_STAT_COPYRIGHT,
    zend_stat_startup,
    zend_stat_shutdown,
    zend_stat_activate,
    zend_stat_deactivate,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    STANDARD_ZEND_EXTENSION_PROPERTIES
};

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(zend_stat_api_returns_bool_arginfo, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(zend_stat_api_returns_long_arginfo, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(zend_stat_api_returns_double_arginfo, 0, 0, IS_DOUBLE, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(zend_stat_api_returns_array_arginfo, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_FUNCTION(zend_stat_pid)
{
    ZEND_PARSE_PARAMETERS_START(0, 0)
    ZEND_PARSE_PARAMETERS_END();

    RETURN_LONG(zend_stat_pid());
}

ZEND_FUNCTION(zend_stat_elapsed)
{
    ZEND_PARSE_PARAMETERS_START(0, 0)
    ZEND_PARSE_PARAMETERS_END();

    RETURN_DOUBLE(zend_stat_time());
}

static zend_bool zend_stat_buffer_consume_u(zend_stat_sample_t *sample, zval *return_value) {
    array_init(return_value);

    if (sample->type == ZEND_STAT_SAMPLE_MEMORY) {
        
    }

    return ZEND_STAT_BUFFER_CONSUMER_STOP;
}

ZEND_FUNCTION(zend_stat_buffer_consume) 
{
    ZEND_PARSE_PARAMETERS_START(0, 0)
    ZEND_PARSE_PARAMETERS_END();

    zend_stat_buffer_consume(
        zend_stat_buffer,
        (zend_stat_buffer_consumer_t) zend_stat_buffer_consume_u, 
        return_value, 1);
}

static zend_function_entry zend_stat_api[] = {
    ZEND_NS_FENTRY("stat",          pid,        ZEND_FN(zend_stat_pid),                zend_stat_api_returns_long_arginfo,   0)
    ZEND_NS_FENTRY("stat",          elapsed,    ZEND_FN(zend_stat_elapsed),            zend_stat_api_returns_double_arginfo, 0)
    ZEND_NS_FENTRY("stat\\sampler", activate,   ZEND_FN(zend_stat_sampler_activate),   zend_stat_api_returns_bool_arginfo,   0)
    ZEND_NS_FENTRY("stat\\sampler", active,     ZEND_FN(zend_stat_sampler_active),     zend_stat_api_returns_bool_arginfo,   0)
    ZEND_NS_FENTRY("stat\\sampler", deactivate, ZEND_FN(zend_stat_sampler_deactivate), zend_stat_api_returns_bool_arginfo,   0)
    ZEND_NS_FENTRY("stat\\buffer",  consume,    ZEND_FN(zend_stat_buffer_consume),     zend_stat_api_returns_array_arginfo,  0)
    ZEND_FE_END
};

static int zend_stat_startup(zend_extension *ze) {
    zend_stat_ini_startup();

    if (!zend_stat_ini_stream && !zend_stat_ini_dump) {
        zend_error(E_WARNING,
            "[STAT] stream and dump are both disabled by configuration, "
            "may be misconfigured");
        zend_stat_ini_shutdown();

        return SUCCESS;
    }

    if (!zend_stat_strings_startup(zend_stat_ini_strings)) {
        zend_stat_ini_shutdown();

        return SUCCESS;
    }

    if (!(zend_stat_buffer = zend_stat_buffer_startup(zend_stat_ini_samples))) {
        zend_stat_strings_shutdown();
        zend_stat_ini_shutdown();

        return SUCCESS;
    }

    if (!zend_stat_control_startup(
            &zend_stat_control,
            zend_stat_buffer,
            zend_stat_ini_control)) {
        zend_stat_buffer_shutdown(zend_stat_buffer);
        zend_stat_strings_shutdown();
        zend_stat_ini_shutdown();

        return SUCCESS;
    }

    if (!zend_stat_stream_startup(
            &zend_stat_stream,
            zend_stat_buffer,
            zend_stat_ini_stream)) {
        zend_stat_control_shutdown(&zend_stat_control);
        zend_stat_buffer_shutdown(zend_stat_buffer);
        zend_stat_strings_shutdown();
        zend_stat_ini_shutdown();

        return SUCCESS;
    }

    zend_stat_sampler_startup(
        zend_stat_ini_auto,
        zend_stat_ini_interval,
        zend_stat_ini_arginfo,
        zend_stat_ini_samplers,
        zend_stat_buffer);

    zend_stat_started = zend_stat_time();
    zend_stat_main    = zend_stat_pid();

    ze->handle = 0;

    zend_register_functions(NULL, zend_stat_api, NULL, MODULE_PERSISTENT);

    return SUCCESS;
}

static void zend_stat_shutdown(zend_extension *ze) {
    if (0 == zend_stat_started) {
        return;
    }

    if (zend_stat_pid() != zend_stat_main) {
        return;
    }

    if (zend_stat_ini_dump > 0) {
        zend_stat_buffer_dump(
            zend_stat_buffer, zend_stat_ini_dump);
    }

    zend_stat_control_shutdown(&zend_stat_control);
    zend_stat_stream_shutdown(&zend_stat_stream);
    zend_stat_buffer_shutdown(zend_stat_buffer);
    zend_stat_strings_shutdown();
    zend_stat_ini_shutdown();

    zend_stat_started = 0;
}

static void zend_stat_activate(void) {
#if defined(ZTS) && defined(COMPILE_DL_STAT)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif

    if (0 == zend_stat_started) {
        return;
    }

    zend_stat_sampler_activate(0);
}

static void zend_stat_deactivate(void) {
    if (0 == zend_stat_started) {
        return;
    }

    zend_stat_sampler_deactivate();
}

double zend_stat_time(void) {
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != SUCCESS) {
        return (double) -1;
    }

    return ((double) ts.tv_sec + ts.tv_nsec / 1000000000.00) - zend_stat_started;
}

#if defined(ZTS) && defined(COMPILE_DL_STAT)
    ZEND_TSRMLS_CACHE_DEFINE();
#endif

#endif /* ZEND_STAT */
