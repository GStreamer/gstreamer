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

# FIXME 0.11: rename GstBaseAudioSink to GstAudioBaseSink or merge with GstAudioSink
libgstaudio_@GST_MAJORMINOR@_la_SOURCES = \
	audio.c \
	gstringbuffer.c \
	gstaudioclock.c \
	mixerutils.c \
	multichannel.c \
	gstaudiodecoder.c \
	gstaudioencoder.c \
	gstbaseaudiosink.c \
	gstbaseaudiosrc.c \
	gstaudiofilter.c \
	gstaudiosink.c \
	gstaudiosrc.c \
	gstaudioiec61937.c
nodist_libgstaudio_@GST_MAJORMINOR@_la_SOURCES = $(built_sources) $(built_headers)

libgstaudio_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/audio
libgstaudio_@GST_MAJORMINOR@include_HEADERS = \
	audio.h \
	gstringbuffer.h \
	gstaudioclock.h \
	gstaudiofilter.h \
	gstaudiodecoder.h \
	gstaudioencoder.h \
	gstbaseaudiosink.h \
	gstbaseaudiosrc.h \
	gstaudiosink.h \
	gstaudiosrc.h \
	mixerutils.h \
	multichannel.h \
	gstaudioiec61937.h

nodist_libgstaudio_@GST_MAJORMINOR@include_HEADERS = \
	audio-enumtypes.h

libgstaudio_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstaudio_@GST_MAJORMINOR@_la_LIBADD = \
  $(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
  $(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la \
  $(GST_BASE_LIBS) $(GST_LIBS)
libgstaudio_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

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
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH="" GST_PLUGIN_PATH="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstAudio \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		$(gir_cincludes) \
		--add-include-path=$(builddir)/../interfaces \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_MAJORMINOR@` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_MAJORMINOR@` \
		--library=libgstaudio-@GST_MAJORMINOR@.la \
		--include=Gst-@GST_MAJORMINOR@ \
		--include=GstBase-@GST_MAJORMINOR@ \
		--include=GstInterfaces-@GST_MAJORMINOR@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_MAJORMINOR@ \
		--pkg gstreamer-base-@GST_MAJORMINOR@ \
		--pkg gstreamer-interfaces-@GST_MAJORMINOR@ \
		--pkg-export gstreamer-audio-@GST_MAJORMINOR@ \
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
		--includedir=$(builddir)/../interfaces \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_MAJORMINOR@` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_MAJORMINOR@` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer -:PROJECT libgstaudio -:SHARED libgstaudio-@GST_MAJORMINOR@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstaudio_@GST_MAJORMINOR@_la_SOURCES) \
	           $(nodist_libgstaudio_@GST_MAJORMINOR@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstaudio_@GST_MAJORMINOR@_la_CFLAGS) \
	 -:LDFLAGS $(libgstaudio_@GST_MAJORMINOR@_la_LDFLAGS) \
	           $(libgstaudio_@GST_MAJORMINOR@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_MAJORMINOR@/gst/tag \
	 -:HEADERS $(libgstaudio_@GST_MAJORMINOR@include_HEADERS) \
	           $(nodist_libgstaudio_@GST_MAJORMINOR@include_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
