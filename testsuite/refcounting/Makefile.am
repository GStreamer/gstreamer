# GstObject is no longer instantiatable in glib2
# neither is GstElement
if BUILD_FAILING_TESTS
tests_failing = element
else
tests_failing =
endif
tests_working = bin element_pad pad

element_SOURCES = element.c mem.c
pad_SOURCES = pad.c mem.c

element_pad_SOURCES = element_pad.c mem.c
bin_SOURCES = bin.c mem.c

GST_PLUGIN_PATH = $(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(GST_PLUGIN_PATH) GST_REGISTRY=`pwd`/test-registry.xml

TESTS = $(tests_failing) $(tests_working)

check_PROGRAMS = $(TESTS)

EXTRA_DIST = thread.c object.c

noinst_HEADERS = mem.h

# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)

