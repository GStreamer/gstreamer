lib_LTLIBRARIES = libgstapp-@GST_MAJORMINOR@.la

glib_enum_define = GST_APP
glib_enum_prefix = gst_app

include $(top_srcdir)/common/glib-gen.mak

built_sources = gstapp-marshal.c
built_headers = gstapp-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

nodist_libgstapp_@GST_MAJORMINOR@_la_SOURCES = \
             $(built_sources)

libgstapp_@GST_MAJORMINOR@_la_SOURCES = gstappsrc.c gstappbuffer.c gstappsink.c 
libgstapp_@GST_MAJORMINOR@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstapp_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS)
libgstapp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_ALL_LDFLAGS)
libgstapp_@GST_MAJORMINOR@_la_LIBTOOLFLAGS = --tag=disable-static

libgstapp_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/app
libgstapp_@GST_MAJORMINOR@include_HEADERS = \
	gstappsrc.h \
	gstappbuffer.h \
	gstappsink.h

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gstapp-marshal.list

