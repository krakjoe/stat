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

static zend_stat_buffer_t*     zend_stat_buffer = NULL;
static zend_stat_io_t          zend_stat_stream;
static zend_stat_io_t          zend_stat_control;
static double                  zend_stat_started = 0;
ZEND_TLS zend_stat_sampler_t   zend_stat_sampler;
ZEND_TLS zend_stat_request_t   zend_stat_request;

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

    if (!(zend_stat_buffer = zend_stat_buffer_startup(
            zend_stat_ini_samples,
            zend_stat_ini_interval,
            zend_stat_ini_arginfo,
            zend_stat_ini_samplers))) {
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

    zend_stat_started = zend_stat_time();

    ze->handle = 0;

    return SUCCESS;
}

static void zend_stat_shutdown(zend_extension *ze) {
    if (0 == zend_stat_started) {
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

    if (!zend_stat_request_create(&zend_stat_request)) {
        zend_error(E_WARNING,
            "[STAT] Could not allocate request, "
            "not activating sampler, may be low on memory");
        return;
    }

    if (!zend_stat_buffer_samplers_add(zend_stat_buffer)) {
        return;
    }

    zend_stat_sampler_activate(&zend_stat_sampler, &zend_stat_request, zend_stat_buffer);
}

static void zend_stat_deactivate(void) {
    zend_stat_sampler_deactivate(&zend_stat_sampler);

    zend_stat_buffer_samplers_remove(zend_stat_buffer);

    zend_stat_request_release(&zend_stat_request);
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
