plugin_LTLIBRARIES = libgstsrt.la

libgstsrt_la_SOURCES = \
	gstsrt.c \
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

glib_enum_headers=gstsrt-enums.h
glib_enum_define=GST_SRT
glib_gen_prefix=gst_srt
glib_gen_basename=gstsrt

BUILT_SOURCES = gstsrt-enumtypes.c gstsrt-enumtypes.h
nodist_libgstsrt_la_SOURCES = $(BUILT_SOURCES)

CLEANFILES = $(BUILT_SOURCES)

noinst_HEADERS = \
	gstsrt-enums.h \
	gstsrtobject.h \
	gstsrtsink.h \
	gstsrtsrc.h \
	$(NULL)

include $(top_srcdir)/common/gst-glib-gen.mak
