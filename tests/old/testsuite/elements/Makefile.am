if BUILD_FAILING_TESTS
tests_failing = 
else
tests_failing =
endif
tests_working = fake name tee

GST_PLUGIN_PATH=$(shell cd $(top_builddir) && pwd)

TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(GST_PLUGIN_PATH) GST_REGISTRY=`pwd`test-registry.xml

TESTS = $(tests_failing) $(tests_working)

check_PROGRAMS = $(tests_failing) $(tests_working)

EXTRA_DIST = property.h
LDADD = $(GST_LIBS) 
CFLAGS = $(GST_CFLAGS)

