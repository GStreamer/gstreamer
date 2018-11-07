plugin_LTLIBRARIES = libgstsrt.la

libgstsrt_la_SOURCES = \
	gstsrtobject.c \
	gstsrtsink.c \
	gstsrtsrc.c \
	$(NULL)

libgstsrt_la_CFLAGS = \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_CFLAGS) \
    $(GIO_CFLAGS) \
    $(SRT_CFLAGS) \
    $(NULL)

libgstsrt_la_LIBADD = \
    $(GST_PLUGINS_BASE_LIBS) \
    $(GST_LIBS) \
    $(GIO_LIBS) \
    -lgstbase-1.0 \
    $(SRT_LIBS) \
    $(NULL)

libgstsrt_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

CLEANFILES = $(BUILT_SOURCES)

noinst_HEADERS = \
	gstsrtobject.h \
	gstsrtsink.h \
	gstsrtsrc.h \
	$(NULL)

include $(top_srcdir)/common/gst-glib-gen.mak
