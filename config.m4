dnl config.m4 for extension zstat

PHP_ARG_ENABLE([stat],
  [whether to enable stat support],
  [AS_HELP_STRING([--enable-stat],
    [Enable stat support])],
  [no])

if test "$PHP_STAT" != "no"; then
  PHP_ADD_LIBRARY(pthread,, STAT_SHARED_LIBADD)

  AC_DEFINE(HAVE_ZEND_STAT, 1, [ Have stat support ])

  PHP_NEW_EXTENSION(stat, 
        zend_stat.c \
        zend_stat_buffer.c \
        zend_stat_ini.c \
        zend_stat_io.c \
        zend_stat_sampler.c \
        zend_stat_timer.c \
        zend_stat_strings.c, 
        $ext_shared,,-DZEND_ENABLE_STATIC_TSRMLS_CACHE=1,,yes)

  PHP_SUBST(STAT_SHARED_LIBADD)
fi
