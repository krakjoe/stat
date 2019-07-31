stat-test-coverage:
	CCACHE_DISABLE=1 EXTRA_CFLAGS="-fprofile-arcs -ftest-coverage" TEST_PHP_ARGS="-q" $(MAKE) clean test

stat-test-coverage-lcov: stat-test-coverage
	lcov -c --directory $(top_srcdir)/src/.libs --output-file $(top_srcdir)/coverage.info

stat-test-coverage-html: stat-test-coverage-lcov
	genhtml $(top_srcdir)/coverage.info --output-directory=$(top_srcdir)/html

stat-test-coverage-travis:
	CCACHE_DISABLE=1 EXTRA_CFLAGS="-fprofile-arcs -ftest-coverage" $(MAKE)
