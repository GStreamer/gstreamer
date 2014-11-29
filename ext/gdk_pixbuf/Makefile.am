plugin_LTLIBRARIES = libgstgdkpixbuf.la

# gstgdkanimation.[ch] - GdkPixbuf animations decode everything from the start,
# which means it's easy to make us go OOM with manipulated input, disabled

libgstgdkpixbuf_la_SOURCES = \
	gstgdkpixbufdec.c \
	gstgdkpixbufoverlay.c \
	gstgdkpixbufplugin.c \
	gstgdkpixbufsink.c
libgstgdkpixbuf_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CONTROLLER_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(GDK_PIXBUF_CFLAGS)
libgstgdkpixbuf_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_CONTROLLER_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) $(GDK_PIXBUF_LIBS)
libgstgdkpixbuf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgdkpixbuf_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstgdkpixbufdec.h \
	gstgdkpixbufoverlay.h \
	gstgdkpixbufsink.h
