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

#ifndef ZEND_STAT_ARENA_H
# define ZEND_STAT_ARENA_H

typedef struct _zend_stat_arena_t zend_stat_arena_t;

zend_stat_arena_t* zend_stat_arena_create(zend_long size);
void* zend_stat_arena_alloc(zend_stat_arena_t *arena, zend_long size);
void zend_stat_arena_free(zend_stat_arena_t *arena, void *mem);
void zend_stat_arena_destroy(zend_stat_arena_t *arena);
#endif
