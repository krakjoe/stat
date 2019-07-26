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

#ifndef ZEND_STAT_STRINGS_H
# define ZEND_STAT_STRINGS_H

typedef struct _zend_stat_string_t {
    zend_bool  locked;
    zend_ulong hash;
    zend_long  length;
    char      *value;
} zend_stat_string_t;

zend_bool zend_stat_strings_startup(zend_long strings);
zend_stat_string_t* zend_stat_string(zend_string *string);
zend_stat_string_t *zend_stat_string_opcode(zend_uchar opcode);
void zend_stat_strings_shutdown(void);

#endif	/* ZEND_STAT_STRINGS_H */
