plugin_LTLIBRARIES = libgstaudioconvert.la

ORC_SOURCE=gstaudioconvertorc
include $(top_srcdir)/common/orc.mak

libgstaudioconvert_la_SOURCES = \
	gstaudioconvert.c \
	audioconvert.c \
	gstchannelmix.c \
	gstaudioquantize.c \
	plugin.c
nodist_libgstaudioconvert_la_SOURCES = $(ORC_NODIST_SOURCES)

libgstaudioconvert_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstaudioconvert_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudioconvert_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS) $(LIBM)
libgstaudioconvert_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstaudioconvert.h \
	audioconvert.h \
	gstchannelmix.h \
	gstaudioquantize.h \
	gstfastrandom.h \
	plugin.h
