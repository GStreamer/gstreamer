plugin_LTLIBRARIES = libgstvolume.la

libgstvolume_la_SOURCES = gstvolume.c
libgstvolume_la_CFLAGS = $(GST_CFLAGS)
libgstvolume_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvolume_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-$(GST_MAJORMINOR).la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

noinst_HEADERS = gstvolume.h filter.func

if HAVE_GTK
noinst_PROGRAMS = demo
demo_SOURCES = demo.c
demo_CFLAGS = $(GTK_CFLAGS) $(GST_CFLAGS) -D_GNU_SOURCE
demo_LDFLAGS = $(GTK_LIBS) $(GST_LIBS) -lm
endif
