
plugin_LTLIBRARIES = libgstopensles.la

libgstopensles_la_SOURCES = openslescommon.c     \
                            openslesringbuffer.c \
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

noinst_HEADERS = openslescommon.h     \
                 openslesringbuffer.h \
                 openslessink.h       \
                 openslessrc.h        \
                 opensles.h

