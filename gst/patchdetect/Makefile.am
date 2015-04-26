plugin_LTLIBRARIES = libgstpatchdetect.la

#ORC_SOURCE=gstpatchdetectorc
#include $(top_srcdir)/common/orc.mak

libgstpatchdetect_la_SOURCES = gstpatchdetect.c
#nodist_libgstpatchdetect_la_SOURCES = $(ORC_NODIST_SOURCES)
libgstpatchdetect_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstpatchdetect_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM)
libgstpatchdetect_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpatchdetect_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstpatchdetect.h
