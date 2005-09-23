
plugin_LTLIBRARIES = libgstcairo.la

noinst_HEADERS = gsttimeoverlay.h gsttextoverlay.h

libgstcairo_la_SOURCES = \
	gsttimeoverlay.c \
	gsttextoverlay.c \
	gstcairo.c
libgstcairo_la_CFLAGS = $(GST_CFLAGS) $(CAIRO_CFLAGS) \
	-I$(top_srcdir)/gst/videofilter
libgstcairo_la_LIBADD = $(GST_LIBS) $(CAIRO_LIBS) -lm
libgstcairo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

