plugin_LTLIBRARIES = 

if USE_GDK_PIXBUF
plugin_LTLIBRARIES += libgstgdkpixbuf.la

endif

if USE_GDK_PIXBUF3
plugin_LTLIBRARIES += libgstgdkpixbuf3.la
endif

libgstgdkpixbuf_la_SOURCES = gstgdkpixbuf.c gstgdkpixbufsink.c pixbufscale.c
libgstgdkpixbuf_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(GDK_PIXBUF_CFLAGS)
libgstgdkpixbuf_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) $(GDK_PIXBUF_LIBS)
libgstgdkpixbuf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgdkpixbuf_la_LIBTOOLFLAGS = --tag=disable-static

libgstgdkpixbuf3_la_SOURCES = gstgdkpixbuf.c gstgdkpixbufsink.c pixbufscale.c
libgstgdkpixbuf3_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(GDK_PIXBUF3_CFLAGS) \
	-DGDK_PIXBUF3=1
libgstgdkpixbuf3_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) $(GDK_PIXBUF3_LIBS)
libgstgdkpixbuf3_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgdkpixbuf3_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstgdkpixbuf.h \
	gstgdkpixbufsink.h \
	pixbufscale.h

