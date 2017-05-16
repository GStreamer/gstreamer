plugin_LTLIBRARIES = libgstbayer.la

ORC_SOURCE=gstbayerorc
include $(top_srcdir)/common/orc.mak

# orc-generated code creates warnings
ERROR_CFLAGS=

libgstbayer_la_SOURCES = \
	gstbayer.c \
	gstbayer2rgb.c \
	gstrgb2bayer.c \
	gstrgb2bayer.h
libgstbayer_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
    $(ORC_CFLAGS) \
    $(GST_CFLAGS)
libgstbayer_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
    $(ORC_LIBS) \
    $(GST_BASE_LIBS)
libgstbayer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
nodist_libgstbayer_la_SOURCES = $(ORC_NODIST_SOURCES)
