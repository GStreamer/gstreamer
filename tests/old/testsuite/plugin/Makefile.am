TOP_BUILDDIR=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(TOP_BUILDDIR) GST_REGISTRY=$(TOP_BUILDDIR)/testsuite/test-registry.xml

plugindir = $(shell cd $(top_builddir)/testsuite/plugin/ && pwd)

plugin_LTLIBRARIES = libtestplugin.la libtestplugin2.la

# FIXME : tests need fixing
testprogs = static dynamic linked loading registry static2

AM_CFLAGS = $(GST_CFLAGS)
LIBS = $(GST_LIBS)

libtestplugin_la_SOURCES = testplugin.c
libtestplugin_la_CFLAGS = $(GST_CFLAGS)
libtestplugin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libtestplugin2_la_SOURCES = testplugin2.c
libtestplugin2_la_CFLAGS = $(GST_CFLAGS)
libtestplugin2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

linked_LDFLAGS = -L. -ltestplugin -ltestplugin2

static_SOURCES = static.c testplugin_s.c testplugin2_s.c

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)
