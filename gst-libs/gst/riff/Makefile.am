lib_LTLIBRARIES = libgstriff-@GST_MAJORMINOR@.la

libgstriff_@GST_MAJORMINOR@_la_SOURCES = \
	riff.c \
	riff-media.c \
	riff-read.c

libgstriff_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/riff
libgstriff_@GST_MAJORMINOR@include_HEADERS = \
	riff-ids.h \
	riff-media.h \
	riff-read.h

libgstriff_@GST_MAJORMINOR@_la_LIBADD = \
  $(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la \
  $(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_MAJORMINOR@.la \
  $(GST_BASE_LIBS) $(GST_LIBS)

libgstriff_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstriff_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstRiff-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstriff_@GST_MAJORMINOR@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstriff_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/riff/%',$(libgstriff_@GST_MAJORMINOR@include_HEADERS))

GstRiff-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstriff-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstRiff \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		--add-include-path=$(builddir)/../tag \
		--add-include-path=$(builddir)/../audio \
		--add-include-path=$(builddir)/../interfaces \
		--library=libgstriff-0.10.la \
		--include=GstAudio-0.10 \
		--include=GstTag-0.10 \
		--include=Gst-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
		--pkg gstreamer-tag-0.10 \
		--pkg gstreamer-audio-0.10 \
		--pkg gstreamer-interfaces-0.10 \
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
		--includedir=$(builddir)/../tag \
		--includedir=$(builddir)/../audio \
		--includedir=$(builddir)/../interfaces \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES = $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
