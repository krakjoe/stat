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

#ifndef ZEND_STAT_INI
# define ZEND_STAT_INI

#include "zend_stat.h"
#include "zend_stat_ini.h"

zend_long    zend_stat_ini_slots     = -1;
zend_long    zend_stat_ini_interval = -1;
zend_long    zend_stat_ini_strings   = -1;
char*        zend_stat_ini_socket    = NULL;
int          zend_stat_ini_dump      = -1;

static ZEND_INI_MH(zend_stat_ini_update_slots)
{
    if (UNEXPECTED(zend_stat_ini_slots != -1)) {
        return FAILURE;
    }

    zend_stat_ini_slots =
        zend_atol(
            ZSTR_VAL(new_value),
            ZSTR_LEN(new_value));

    return SUCCESS;
}

static ZEND_INI_MH(zend_stat_ini_update_interval)
{
    if (UNEXPECTED(zend_stat_ini_interval != -1)) {
        return FAILURE;
    }

    zend_stat_ini_interval =
        zend_atol(
            ZSTR_VAL(new_value),
            ZSTR_LEN(new_value));

    return SUCCESS;
}

static ZEND_INI_MH(zend_stat_ini_update_strings)
{
    if (UNEXPECTED(zend_stat_ini_strings != -1)) {
        return FAILURE;
    }

    zend_stat_ini_strings =
        zend_atol(
            ZSTR_VAL(new_value),
            ZSTR_LEN(new_value));

    return SUCCESS;
}

static ZEND_INI_MH(zend_stat_ini_update_socket)
{
    int skip = FAILURE;

    if (UNEXPECTED(NULL != zend_stat_ini_socket)) {
        return FAILURE;
    }

    if (sscanf(ZSTR_VAL(new_value), "%d", &skip) == 1) {
        if (SUCCESS == skip) {
            return SUCCESS;
        }
    }

    zend_stat_ini_socket = pestrndup(ZSTR_VAL(new_value), ZSTR_LEN(new_value), 1);

    return SUCCESS;
}

static ZEND_INI_MH(zend_stat_ini_update_dump)
{
    if (UNEXPECTED(-1 != zend_stat_ini_dump)) {
        return FAILURE;
    }

    zend_stat_ini_dump = zend_atoi(ZSTR_VAL(new_value), ZSTR_LEN(new_value));

    return SUCCESS;
}

ZEND_INI_BEGIN()
    ZEND_INI_ENTRY("stat.slots",     "10000",             ZEND_INI_SYSTEM, zend_stat_ini_update_slots)
    ZEND_INI_ENTRY("stat.interval",  "1000",              ZEND_INI_SYSTEM, zend_stat_ini_update_interval)
    ZEND_INI_ENTRY("stat.strings",   "32M",               ZEND_INI_SYSTEM, zend_stat_ini_update_strings)
    ZEND_INI_ENTRY("stat.socket",    "zend.stat.socket",  ZEND_INI_SYSTEM, zend_stat_ini_update_socket)
    ZEND_INI_ENTRY("stat.dump",      "0",                 ZEND_INI_SYSTEM, zend_stat_ini_update_dump)
ZEND_INI_END()

void zend_stat_ini_startup() {
    zend_register_ini_entries(ini_entries, -1);
}

void zend_stat_ini_shutdown() {
    zend_unregister_ini_entries(-1);

    pefree(zend_stat_ini_socket, 1);
}
#endif	/* ZEND_STAT_INI */
