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
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH="" GST_PLUGIN_PATH="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstRiff \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		--add-include-path=$(builddir)/../tag \
		--add-include-path=$(builddir)/../audio \
		--add-include-path=$(builddir)/../interfaces \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_MAJORMINOR@` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_MAJORMINOR@` \
		--library=libgstriff-@GST_MAJORMINOR@.la \
		--include=GstAudio-@GST_MAJORMINOR@ \
		--include=GstTag-@GST_MAJORMINOR@ \
		--include=Gst-@GST_MAJORMINOR@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_MAJORMINOR@ \
		--pkg gstreamer-tag-@GST_MAJORMINOR@ \
		--pkg gstreamer-audio-@GST_MAJORMINOR@ \
		--pkg gstreamer-interfaces-@GST_MAJORMINOR@ \
		--pkg-export gstreamer-riff-@GST_MAJORMINOR@ \
		--add-init-section="gst_init(NULL,NULL);" \
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
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		$(INTROSPECTION_COMPILER) \
		--includedir=$(srcdir) \
		--includedir=$(builddir) \
		--includedir=$(builddir)/../tag \
		--includedir=$(builddir)/../audio \
		--includedir=$(builddir)/../interfaces \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_MAJORMINOR@` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_MAJORMINOR@` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES = $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif

Android.mk: Makefile.am
	androgenizer -:PROJECT libgstriff -:SHARED libgstriff-@GST_MAJORMINOR@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstriff_@GST_MAJORMINOR@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstriff_@GST_MAJORMINOR@_la_CFLAGS) \
	 -:LDFLAGS $(libgstriff_@GST_MAJORMINOR@_la_LDFLAGS) \
	           $(libgstriff_@GST_MAJORMINOR@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_MAJORMINOR@/gst/tag \
	 -:HEADERS $(libgstriff_@GST_MAJORMINOR@include_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
