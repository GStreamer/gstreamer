plugin_LTLIBRARIES = libgstlevel.la

libgstlevel_la_SOURCES = gstlevel.c
libgstlevel_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstlevel_la_LIBADD = $(GST_BASE_LIBS)
libgstlevel_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstlevel.h

noinst_PROGRAMS = level-example
level_example_CFLAGS = $(GST_CFLAGS)
level_example_LDADD = $(GST_LIBS)
