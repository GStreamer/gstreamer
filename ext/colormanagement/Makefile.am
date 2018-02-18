plugin_LTLIBRARIES = libgstcolormanagement.la

libgstcolormanagement_la_SOURCES = gstcolormanagement.c \
	gstlcms.c

libgstcolormanagement_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(LCMS2_CFLAGS)
libgstcolormanagement_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-1.0 $(GST_BASE_LIBS) $(GST_LIBS) $(LCMS2_LIBS)
libgstcolormanagement_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcolormanagement_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstlcms.h
