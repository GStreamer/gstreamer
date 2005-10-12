plugin_LTLIBRARIES = libgstcairo.la

noinst_HEADERS = gsttimeoverlay.h # gsttextoverlay.h

libgstcairo_la_SOURCES = \
	gsttimeoverlay.c \
	gstcairo.c

#	gsttextoverlay.c

libgstcairo_la_CFLAGS = \
	-I$(top_srcdir)/gst/videofilter \
        $(GST_CFLAGS) $(CAIRO_CFLAGS)
libgstcairo_la_LIBADD = \
  $(top_builddir)/gst/videofilter/libgstvideofilter-$(GST_MAJORMINOR).la \
  $(GST_LIBS) $(CAIRO_LIBS) -lm
libgstcairo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
