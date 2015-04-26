plugin_LTLIBRARIES = libgstgaudieffects.la

ORC_SOURCE=gstgaudieffectsorc
include $(top_srcdir)/common/orc.mak

libgstgaudieffects_la_SOURCES = \
        gstburn.c \
        gstchromium.c \
        gstdilate.c \
        gstdodge.c \
        gstexclusion.c \
        gstgaussblur.c \
        gstsolarize.c \
        gstplugin.c
nodist_libgstgaudieffects_la_SOURCES = $(ORC_NODIST_SOURCES)

libgstgaudieffects_la_CFLAGS = \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_CFLAGS) \
    $(ORC_CFLAGS)

libgstgaudieffects_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
    $(GST_BASE_LIBS) \
    $(GST_LIBS) \
    $(LIBM) \
    $(ORC_LIBS)
libgstgaudieffects_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgaudieffects_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

# headers we need but don't want installed
noinst_HEADERS = \
        gstburn.h \
        gstchromium.h \
        gstdilate.h \
        gstdodge.h \
        gstexclusion.h \
        gstgaussblur.h \
        gstplugin.h \
        gstsolarize.h

EXTRA_SCRIPTS = blur-example.py  burn-example.py
