# variables used for enum/marshal generation
glib_enum_headers= 		\
	multichannel.h		\
	gstringbuffer.h

glib_enum_define = GST_AUDIO
glib_gen_prefix = gst_audio
glib_gen_basename = audio

built_sources = audio-enumtypes.c
built_headers = audio-enumtypes.h
BUILT_SOURCES = $(built_sources) $(built_headers)

lib_LTLIBRARIES = \
	libgstaudio-@GST_MAJORMINOR@.la

CLEANFILES = $(BUILT_SOURCES)

libgstaudio_@GST_MAJORMINOR@_la_SOURCES = \
	audio.c \
	gstringbuffer.c \
	gstaudioclock.c \
	mixerutils.c \
	multichannel.c \
	gstbaseaudiosink.c \
	gstbaseaudiosrc.c \
	gstaudiofilter.c \
	gstaudiosink.c \
	gstaudiosrc.c
nodist_libgstaudio_@GST_MAJORMINOR@_la_SOURCES = $(built_sources) $(built_headers)

libgstaudio_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/audio
libgstaudio_@GST_MAJORMINOR@include_HEADERS = \
	audio.h \
	gstringbuffer.h \
	gstaudioclock.h \
	gstaudiofilter.h \
	gstbaseaudiosink.h \
	gstbaseaudiosrc.h \
	gstaudiosink.h \
	gstaudiosrc.h \
	mixerutils.h \
	multichannel.h

nodist_libgstaudio_@GST_MAJORMINOR@include_HEADERS = \
	audio-enumtypes.h

libgstaudio_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstaudio_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) \
  $(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la
libgstaudio_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

noinst_PROGRAMS = testchannels
testchannels_SOURCES = testchannels.c
testchannels_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
testchannels_LDADD = $(GST_LIBS)

include $(top_srcdir)/common/gst-glib-gen.mak

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstAudio-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstaudio_@GST_MAJORMINOR@include_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(nodist_libgstaudio_@GST_MAJORMINOR@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstaudio_@GST_MAJORMINOR@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(nodist_libgstaudio_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/audio/%',$(libgstaudio_@GST_MAJORMINOR@include_HEADERS))
gir_cincludes+=$(patsubst %,--c-include='gst/audio/%',$(nodist_libgstaudio_@GST_MAJORMINOR@include_HEADERS))

GstAudio-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstaudio-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstAudio \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		$(gir_cincludes) \
		--add-include-path=$(builddir)/../interfaces \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		--library=libgstaudio-0.10.la \
		--include=Gst-0.10 \
		--include=GstBase-0.10 \
		--include=GstInterfaces-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
		--pkg gstreamer-base-0.10 \
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
		--includedir=$(builddir)/../interfaces \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
