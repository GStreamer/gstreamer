TOP_BUILDDIR=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(TOP_BUILDDIR) GST_REGISTRY=$(TOP_BUILDDIR)/testsuite/test-registry.xml

if BUILD_FAILING_TESTS
tests_failing = fake
else
tests_failing =
endif
tests_working = name tee property

TESTS = $(tests_working)

check_PROGRAMS = $(tests_failing) $(tests_working)

EXTRA_DIST = property.h
LDADD = $(GST_LIBS) 
AM_CFLAGS = $(GST_CFLAGS)

