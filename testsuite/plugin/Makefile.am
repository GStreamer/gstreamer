plugindir = $(builddir)/testsuite/plugin/

GST_PLUGIN_PATH=$(shell cd $(top_builddir) && pwd)
TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(GST_PLUGIN_PATH) GST_REGISTRY=$(GST_PLUGIN_PATH)/testsuite/test-registry.xml

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
