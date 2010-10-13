plugin_LTLIBRARIES = libgsth263parse.la

libgsth263parse_la_SOURCES = plugin.c \
	h263parse.c gsth263parse.c gsth264parse.c h264parse.c
libgsth263parse_la_CFLAGS = $(GST_CFLAGS)
libgsth263parse_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgsth263parse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsth263parse_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gsth263parse.h h263parse.h \
	gsth264parse.h h264parse.h
