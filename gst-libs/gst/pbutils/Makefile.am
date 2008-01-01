lib_LTLIBRARIES = libgstpbutils-@GST_MAJORMINOR@.la

headers_pbutils = \
	pbutils.h         \
	descriptions.h    \
	install-plugins.h \
	missing-plugins.h

# variables used for enum/marshal generation
glib_enum_headers=$(headers_pbutils)
glib_enum_define=GST_INSTALL
glib_enum_prefix=gst_install

built_sources = \
	pbutils-enumtypes.c

built_headers = \
	pbutils-enumtypes.h

libgstpbutils_@GST_MAJORMINOR@_la_SOURCES = \
	pbutils.c         \
	descriptions.c    \
	install-plugins.c \
	missing-plugins.c

nodist_libgstpbutils_@GST_MAJORMINOR@_la_SOURCES = \
	$(built_sources)

libgstpbutils_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/pbutils
libgstpbutils_@GST_MAJORMINOR@include_HEADERS = \
	$(headers_pbutils)
nodist_libgstpbutils_@GST_MAJORMINOR@include_HEADERS = \
	pbutils-enumtypes.h

libgstpbutils_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstpbutils_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstpbutils_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

BUILT_SOURCES = \
	$(built_sources) \
	$(built_headers)

CLEANFILES = $(BUILT_SOURCES)


include $(top_srcdir)/common/glib-gen.mak
