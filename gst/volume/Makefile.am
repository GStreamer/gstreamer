
plugin_LTLIBRARIES = libgstvolume.la

libgstvolume_la_SOURCES = gstvolume.c
libgstvolume_la_CFLAGS = $(GST_CFLAGS)
libgstvolume_la_LIBADD = $(GST_LIBS) $(GST_CONTROL_LIBS) \
	$(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la
libgstvolume_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstvolume.h filter.func

if HAVE_GTK
noinst_PROGRAMS = demo
demo_SOURCES = demo.c
demo_CFLAGS = $(GTK_CFLAGS) $(GST_CFLAGS) -D_GNU_SOURCE
demo_LDFLAGS = $(GTK_LIBS) $(GST_LIBS) -lm
endif
