TESTS_ENVIRONMENT = GST_PLUGIN_PATH=$(top_builddir) GST_REGISTRY=$(top_builddir)/testsuite/test-registry.xml

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@
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

linked_LIBS = libtestplugin.la libtestplugin2.la

static_SOURCES = static.c testplugin_s.c testplugin2_s.c

check_PROGRAMS = $(testprogs)

TESTS = $(top_builddir)/tools/gst-register $(testprogs)

# override to _not_ install the test plugins
install-pluginLTLIBRARIES:

