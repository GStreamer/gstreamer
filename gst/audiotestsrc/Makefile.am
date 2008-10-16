plugin_LTLIBRARIES = libgstaudiotestsrc.la

libgstaudiotestsrc_la_SOURCES = gstaudiotestsrc.c
libgstaudiotestsrc_la_CFLAGS = $(GST_CFLAGS) $(GST_CONTROLLER_CFLAGS)
libgstaudiotestsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiotestsrc_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(GST_CONTROLLER_LIBS) $(LIBM)
libgstaudiotestsrc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstaudiotestsrc.h
