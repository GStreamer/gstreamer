TOP_BUILDDIR=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(TOP_BUILDDIR) GST_REGISTRY=$(TOP_BUILDDIR)/testsuite/test-registry.xml

if BUILD_FAILING_TESTS
tests_failing = element bin element_pad pad
else
tests_failing =
endif
tests_working = mainloop sched

element_SOURCES = element.c mem.c
pad_SOURCES = pad.c mem.c

element_pad_SOURCES = element_pad.c mem.c
bin_SOURCES = bin.c mem.c

TESTS = $(tests_working)

check_PROGRAMS = $(TESTS)
noinst_PROGRAMS = $(tests_failing)

EXTRA_DIST = thread.c object.c

noinst_HEADERS = mem.h

# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
AM_CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)

