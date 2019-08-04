dnl config.m4 for extension zstat

PHP_ARG_ENABLE([stat],
  [whether to enable stat support],
  [AS_HELP_STRING([--enable-stat],
    [Enable stat support])],
  [no])

PHP_ARG_ENABLE([stat-coverage],
  [whether to enable stat coverage support],
  [AS_HELP_STRING([--enable-stat-coverage],
    [Enable stat coverage support])],
  [no], [no])

PHP_ARG_ENABLE([stat-arena-debug],
  [whether to enable stat arena debug support],
  [AS_HELP_STRING([--enable-stat-arena-debug],
    [Enable stat arena debug support])],
  [no], [no])

if test "$PHP_STAT" != "no"; then
  PHP_ADD_LIBRARY(pthread,, STAT_SHARED_LIBADD)

  AC_DEFINE(HAVE_ZEND_STAT, 1, [ Have stat support ])

  if test "$PHP_STAT_ARENA_DEBUG" != "no"; then
    AC_DEFINE(ZEND_STAT_ARENA_DEBUG, 1, [ Have stat arena debug support ])
  else
    AC_DEFINE(ZEND_STAT_ARENA_DEBUG, 0, [ Do not have stat arena debug support ])
  fi

  PHP_NEW_EXTENSION(stat,
        zend_stat.c \
        src/zend_stat_arena.c \
        src/zend_stat_buffer.c \
        src/zend_stat_ini.c \
        src/zend_stat_io.c \
        src/zend_stat_request.c \
        src/zend_stat_stream.c \
        src/zend_stat_control.c \
        src/zend_stat_sampler.c \
        src/zend_stat_sample.c \
        src/zend_stat_strings.c,
        $ext_shared,,-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1,,yes)

  PHP_ADD_BUILD_DIR($ext_builddir/src, 1)
  PHP_ADD_INCLUDE($ext_srcdir/src)
  PHP_ADD_INCLUDE($ext_srcdir)

  AC_MSG_CHECKING([stat coverage])
  if test "$PHP_STAT_COVERAGE" != "no"; then
    AC_MSG_RESULT([enabled])

    PHP_ADD_MAKEFILE_FRAGMENT
  else
    AC_MSG_RESULT([disabled])
  fi


  PHP_SUBST(STAT_SHARED_LIBADD)
fi
