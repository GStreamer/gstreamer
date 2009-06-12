plugin_LTLIBRARIES = libgstfrei0r.la

libgstfrei0r_la_SOURCES = \
	gstfrei0r.c \
	gstfrei0rfilter.c \
	gstfrei0rsrc.c

libgstfrei0r_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstfrei0r_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_MAJORMINOR@
libgstfrei0r_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstfrei0r_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstfrei0r.h gstfrei0rfilter.h gstfrei0rsrc.h frei0r.h
