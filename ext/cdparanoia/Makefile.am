plugin_LTLIBRARIES = libgstcdparanoia.la

libgstcdparanoia_la_SOURCES = gstcdparanoiasrc.c
libgstcdparanoia_la_CFLAGS = $(GST_CFLAGS)
libgstcdparanoia_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcdparanoia_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/cdda/libgstcdda-@GST_MAJORMINOR@.la \
	$(CDPARANOIA_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

noinst_HEADERS = gstcdparanoiasrc.h


