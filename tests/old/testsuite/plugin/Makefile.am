
include ../Rules

plugin_LTLIBRARIES = libtestplugin.la libtestplugin2.la

tests_pass = dynamic linked loading registry static static2
tests_fail =


libtestplugin_la_SOURCES = testplugin.c
libtestplugin_la_CFLAGS = $(GST_CFLAGS)
libtestplugin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libtestplugin2_la_SOURCES = testplugin2.c
libtestplugin2_la_CFLAGS = $(GST_CFLAGS)
libtestplugin2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

linked_LIBS = libtestplugin.la libtestplugin2.la

static_SOURCES = static.c

