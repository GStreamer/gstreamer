# variables used for enum/marshal generation
ORC_SOURCE=gstaudiopack
include $(top_srcdir)/common/orc.mak

glib_enum_headers= 		\
	audio.h			\
	audio-format.h			\
	audio-channels.h			\
	audio-channel-mixer.h			\
	audio-converter.h 	\
	audio-info.h			\
	audio-quantize.h			\
	audio-resampler.h			\
	gstaudioringbuffer.h

glib_enum_define = GST_AUDIO
glib_gen_prefix = gst_audio
glib_gen_basename = audio

built_sources = audio-enumtypes.c
built_headers = audio-enumtypes.h
BUILT_SOURCES += $(built_sources) $(built_headers)

lib_LTLIBRARIES = \
	libgstaudio-@GST_API_VERSION@.la

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST += dbesi0.c

libgstaudio_@GST_API_VERSION@_la_SOURCES = \
	audio.c \
	audio-format.c \
	audio-channels.c \
	audio-channel-mixer.c \
	audio-converter.c \
	audio-info.c \
	audio-quantize.c \
	audio-resampler.c \
	gstaudioringbuffer.c \
	gstaudioclock.c \
	gstaudiocdsrc.c \
	gstaudiodecoder.c \
	gstaudioencoder.c \
	gstaudiobasesink.c \
	gstaudiobasesrc.c \
	gstaudiofilter.c \
	gstaudiometa.c \
	gstaudiosink.c \
	gstaudiosrc.c \
	gstaudioutilsprivate.c \
	streamvolume.c \
	gstaudioiec61937.c

nodist_libgstaudio_@GST_API_VERSION@_la_SOURCES = $(BUILT_SOURCES)

libgstaudio_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/audio
libgstaudio_@GST_API_VERSION@include_HEADERS = \
	audio.h \
	audio-format.h \
	audio-channels.h \
	audio-channel-mixer.h \
	audio-converter.h \
	audio-info.h \
	audio-quantize.h \
	audio-resampler.h \
	gstaudioringbuffer.h \
	gstaudioclock.h \
	gstaudiofilter.h \
	gstaudiocdsrc.h \
	gstaudiodecoder.h \
	gstaudioencoder.h \
	gstaudiobasesink.h \
	gstaudiobasesrc.h \
	gstaudiometa.h \
	gstaudiosink.h \
	gstaudiosrc.h \
	streamvolume.h \
	gstaudioiec61937.h

nodist_libgstaudio_@GST_API_VERSION@include_HEADERS = \
	audio-enumtypes.h

noinst_HEADERS = \
	gstaudioutilsprivate.h 		\
	audio-resampler-private.h 	\
	audio-resampler-macros.h 	\
	audio-resampler-x86.h 		\
	audio-resampler-x86-sse.h	\
	audio-resampler-x86-sse2.h	\
	audio-resampler-x86-sse41.h	\
	audio-resampler-neon.h

libgstaudio_@GST_API_VERSION@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
		$(ORC_CFLAGS)
libgstaudio_@GST_API_VERSION@_la_LIBADD = \
  $(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_API_VERSION@.la \
  $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM) $(ORC_LIBS)
libgstaudio_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)


# Arch-specific bits

noinst_LTLIBRARIES =

if HAVE_X86
# Don't use full GST_LT_LDFLAGS in LDFLAGS because we get things like
# -version-info that cause a warning on private libs

noinst_LTLIBRARIES += libaudio_resampler_sse.la
libaudio_resampler_sse_la_SOURCES = audio-resampler-x86-sse.c
libaudio_resampler_sse_la_CFLAGS = \
	$(libgstaudio_@GST_API_VERSION@_la_CFLAGS) \
	$(SSE_CFLAGS)
libaudio_resampler_sse_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
libgstaudio_@GST_API_VERSION@_la_LIBADD += libaudio_resampler_sse.la

noinst_LTLIBRARIES += libaudio_resampler_sse2.la
libaudio_resampler_sse2_la_SOURCES = audio-resampler-x86-sse2.c
libaudio_resampler_sse2_la_CFLAGS = \
	$(libgstaudio_@GST_API_VERSION@_la_CFLAGS) \
	$(SSE2_CFLAGS)
libaudio_resampler_sse2_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
libgstaudio_@GST_API_VERSION@_la_LIBADD += libaudio_resampler_sse2.la

noinst_LTLIBRARIES += libaudio_resampler_sse41.la
libaudio_resampler_sse41_la_SOURCES = audio-resampler-x86-sse41.c
libaudio_resampler_sse41_la_CFLAGS = \
	$(libgstaudio_@GST_API_VERSION@_la_CFLAGS) \
	$(SSE41_CFLAGS)
libaudio_resampler_sse41_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
libgstaudio_@GST_API_VERSION@_la_LIBADD += libaudio_resampler_sse41.la

endif


# Introspection

include $(top_srcdir)/common/gst-glib-gen.mak

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstAudio-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstaudio_@GST_API_VERSION@include_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(built_headers))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstaudio_@GST_API_VERSION@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(built_sources))

GstAudio-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstaudio-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" GI_SCANNER_DISABLE_CACHE=yes\
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
		CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" PKG_CONFIG="$(PKG_CONFIG)" DLLTOOL="$(DLLTOOL)" \
		$(INTROSPECTION_SCANNER) -v --namespace GstAudio \
		--nsversion=@GST_API_VERSION@ \
		--warn-all \
		--identifier-prefix=Gst \
		--symbol-prefix=gst \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--c-include "gst/audio/audio.h" \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
		--add-include-path="$(top_builddir)/gst-libs/gst/tag/" \
		--library=libgstaudio-@GST_API_VERSION@.la \
		--library-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=libdir gstreamer-@GST_API_VERSION@` \
		--library-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=libdir gstreamer-base-@GST_API_VERSION@` \
		--library-path="$(top_builddir)/gst-libs/gst/tag/" \
		--include=Gst-@GST_API_VERSION@ \
		--include=GstBase-@GST_API_VERSION@ \
		--include=GstTag-@GST_API_VERSION@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg gstreamer-base-@GST_API_VERSION@ \
		--pkg-export gstreamer-audio-@GST_API_VERSION@ \
		--add-init-section="$(INTROSPECTION_INIT)" \
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
		--includedir="$(top_builddir)/gst-libs/gst/tag/" \
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
