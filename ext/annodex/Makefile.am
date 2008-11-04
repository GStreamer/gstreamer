plugin_LTLIBRARIES = libgstannodex.la

libgstannodex_la_SOURCES = \
	gstannodex.c \
	gstcmmlutils.c \
	gstcmmldec.c \
	gstcmmlenc.c \
	gstcmmltag.c \
	gstcmmlparser.c

libgstannodex_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
			  $(GST_CFLAGS) $(ANNODEX_CFLAGS)
libgstannodex_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
			  $(GST_LIBS) $(ANNODEX_LIBS) $(LIBM)
libgstannodex_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstannodex_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstannodex.h gstcmmlutils.h gstcmmltag.h gstcmmlparser.h \
	gstcmmldec.h gstcmmlenc.h
