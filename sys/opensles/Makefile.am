
plugin_LTLIBRARIES = libgstopensles.la

libgstopensles_la_SOURCES = openslesringbuffer.c \
                            openslessink.c       \
                            openslessrc.c        \
                            opensles.c

libgstopensles_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
    -Wno-deprecated-declarations
libgstopensles_la_LIBADD =            \
    -lgstinterfaces-@GST_MAJORMINOR@  \
    -lgstaudio-@GST_MAJORMINOR@       \
    $(GST_PLUGINS_BASE_LIBS)          \
    $(GST_BASE_LIBS)                  \
    $(GST_LIBS)
libgstopensles_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lOpenSLES
libgstopensles_la_LIBTOOLFLAGS = --tag=disable-static --tag=CC

noinst_HEADERS = openslesringbuffer.h \
                 openslessink.h       \
                 openslessrc.h

