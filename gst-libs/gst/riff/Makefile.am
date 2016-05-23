lib_LTLIBRARIES = libgstriff-@GST_API_VERSION@.la

libgstriff_@GST_API_VERSION@_la_SOURCES = \
	riff.c \
	riff-media.c \
	riff-read.c

libgstriff_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/riff
libgstriff_@GST_API_VERSION@include_HEADERS = \
	riff.h \
	riff-ids.h \
	riff-media.h \
	riff-read.h

libgstriff_@GST_API_VERSION@_la_LIBADD = \
  $(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_API_VERSION@.la \
  $(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_API_VERSION@.la \
  $(GST_BASE_LIBS) $(GST_LIBS)

libgstriff_@GST_API_VERSION@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstriff_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

# *** GIR DISABLED for this library ***
# 
# This library does not respect the type naming conventions required to be
# usable in binded languages.
#
#if HAVE_INTROSPECTION
#BUILT_GIRSOURCES = GstRiff-@GST_API_VERSION@.gir
#
#gir_headers=$(patsubst %,$(srcdir)/%, $(libgstriff_@GST_API_VERSION@include_HEADERS))
#gir_sources=$(patsubst %,$(srcdir)/%, $(libgstriff_@GST_API_VERSION@_la_SOURCES))
#
#GstRiff-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstriff-@GST_API_VERSION@.la
#	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" GI_SCANNER_DISABLE_CACHE=yes\
#		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
#		CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" PKG_CONFIG="$(PKG_CONFIG)" DLLTOOL="$(DLLTOOL)" \
#		$(INTROSPECTION_SCANNER) -v --namespace GstRiff \
#		--nsversion=@GST_API_VERSION@ \
#		--strip-prefix=Gst \
#		--warn-all \
#		--c-include "gst/riff/riff.h" \
#		--add-include-path=$(builddir)/../tag \
#		--add-include-path=$(builddir)/../audio \
#		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
#		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
#		--library=libgstriff-@GST_API_VERSION@.la \
#		--include=GstAudio-@GST_API_VERSION@ \
#		--include=GstTag-@GST_API_VERSION@ \
#		--include=Gst-@GST_API_VERSION@ \
#		--libtool="$(top_builddir)/libtool" \
#		--pkg gstreamer-@GST_API_VERSION@ \
#		--pkg gstreamer-tag-@GST_API_VERSION@ \
#		--pkg gstreamer-audio-@GST_API_VERSION@ \
#		--pkg-export gstreamer-riff-@GST_API_VERSION@ \
#		--add-init-section="$(INTROSPECTION_INIT)" \
#		--output $@ \
#		$(gir_headers) \
#		$(gir_sources)
#
## INTROSPECTION_GIRDIR/INTROSPECTION_TYPELIBDIR aren't the right place to
## install anything - we need to install inside our prefix.
#girdir = $(datadir)/gir-1.0
#gir_DATA = $(BUILT_GIRSOURCES)
#
#typelibsdir = $(libdir)/girepository-1.0/
#
#typelibs_DATA = $(BUILT_GIRSOURCES:.gir=.typelib)
#
#%.typelib: %.gir $(INTROSPECTION_COMPILER)
#	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
#		$(INTROSPECTION_COMPILER) \
#		--includedir=$(srcdir) \
#		--includedir=$(builddir) \
#		--includedir=$(builddir)/../tag \
#		--includedir=$(builddir)/../audio \
#		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
#		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
#		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)
#
#CLEANFILES = $(BUILT_GIRSOURCES) $(typelibs_DATA)
#endif
