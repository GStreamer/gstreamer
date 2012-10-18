
plugin_LTLIBRARIES = libgstopensles.la

libgstopensles_la_SOURCES = openslesringbuffer.c \
                            openslessink.c       \
                            openslessrc.c        \
                            opensles.c

libgstopensles_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
    -Wno-deprecated-declarations
libgstopensles_la_LIBADD =            \
    -lgstaudio-@GST_API_VERSION@      \
    $(GST_PLUGINS_BASE_LIBS)          \
    $(GST_BASE_LIBS)                  \
    $(GST_LIBS)
libgstopensles_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lOpenSLES
libgstopensles_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = openslesringbuffer.h \
                 openslessink.h       \
                 openslessrc.h

