lib_LTLIBRARIES = libgstphotography-@GST_MAJORMINOR@.la
libgstphotographyincludedir = \
        $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/interfaces

headers_photography = 		\
	photography.h

# variables used for enum/marshal generation
glib_enum_headers=$(headers_photography)
glib_enum_define=GST_PHOTOGRAPHY
glib_gen_prefix=gst_photography
glib_gen_basename=photography

built_sources = \
	photography-enumtypes.c

built_headers = \
	photography-enumtypes.h

libgstphotographyinclude_HEADERS = \
	$(headers_photography)

nodist_libgstphotographyinclude_HEADERS = \
	photography-enumtypes.h

libgstphotography_@GST_MAJORMINOR@_la_SOURCES = \
	photography.c

nodist_libgstphotography_@GST_MAJORMINOR@_la_SOURCES = 	\
	$(built_sources)

libgstphotography_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) -DGST_USE_UNSTABLE_API
libgstphotography_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) 
libgstphotography_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

BUILT_SOURCES = \
	$(built_sources) \
	$(built_headers)

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/gst-glib-gen.mak
