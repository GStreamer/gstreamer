# variables used for enum/marshal generation
glib_enum_headers = video.h
glib_enum_define = GST_VIDEO
glib_gen_prefix = gst_video
glib_gen_basename = video

built_sources = video-enumtypes.c
built_headers = video-enumtypes.h
BUILT_SOURCES = $(built_sources) $(built_headers)

lib_LTLIBRARIES = libgstvideo-@GST_MAJORMINOR@.la

CLEANFILES = $(BUILT_SOURCES)

libgstvideo_@GST_MAJORMINOR@_la_SOURCES = \
	video.c gstvideosink.c gstvideofilter.c
nodist_libgstvideo_@GST_MAJORMINOR@_la_SOURCES = $(BUILT_SOURCES)

libgstvideo_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/video
libgstvideo_@GST_MAJORMINOR@include_HEADERS = \
	video.h gstvideosink.h gstvideofilter.h
nodist_libgstvideo_@GST_MAJORMINOR@include_HEADERS = $(built_headers)

libgstvideo_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstvideo_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstvideo_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

include $(top_srcdir)/common/gst-glib-gen.mak

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstVideo-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstvideo_@GST_MAJORMINOR@include_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(nodist_libgstvideo_@GST_MAJORMINOR@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstvideo_@GST_MAJORMINOR@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(nodist_libgstvideo_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/video/%',$(libgstvideo_@GST_MAJORMINOR@include_HEADERS))
gir_cincludes+=$(patsubst %,--c-include='gst/video/%',$(nodist_libgstvideo_@GST_MAJORMINOR@include_HEADERS))

GstVideo-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstvideo-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstVideo \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		--library=libgstvideo-0.10.la \
		--include=Gst-0.10 \
		--include=GstBase-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
		--pkg gstreamer-base-0.10 \
		--output $@ \
		$(gir_headers) \
		$(gir_sources)

# INTROSPECTION_GIRDIR/INTROSPECTION_TYPELIBDIR aren't the right place to
# install anything - we need to install inside our prefix.
girdir = $(datadir)/gir-1.0
gir_DATA = $(BUILT_GIRSOURCES)

typelibsdir = $(libdir)/girepository-1.0/

typelibs_DATA = $(BUILT_GIRSOURCES:.gir=.typelib)

%.typelib: %.gir $(INTROSPECTION_COMPILER)
	$(AM_V_GEN)$(INTROSPECTION_COMPILER) \
		--includedir=$(srcdir) \
		--includedir=$(builddir) \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
