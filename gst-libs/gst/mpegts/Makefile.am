lib_LTLIBRARIES = libgstmpegts-@GST_API_VERSION@.la

libgstmpegts_@GST_API_VERSION@_la_SOURCES = \
	gstmpegtssection.c \
	gstmpegtsdescriptor.c \
	gst-dvb-descriptor.c \
	gst-dvb-section.c \
	gst-atsc-section.c

libgstmpegts_@GST_API_VERSION@includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/mpegts

noinst_HEADERS = gstmpegts-private.h

libgstmpegts_@GST_API_VERSION@include_HEADERS = \
	gstmpegtssection.h 			\
	gst-atsc-section.h			\
	gst-dvb-section.h			\
	gst-scte-section.h			\
	gstmpegtsdescriptor.h			\
	gst-dvb-descriptor.h			\
	mpegts.h

nodist_libgstmpegts_@GST_API_VERSION@include_HEADERS = \
	gstmpegts-enumtypes.h

libgstmpegts_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS)

libgstmpegts_@GST_API_VERSION@_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

libgstmpegts_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)

glib_enum_headers=gstmpegtssection.h	\
	gstmpegtsdescriptor.h		\
	gst-atsc-section.h		\
	gst-dvb-section.h		\
	gst-scte-section.h		\
	gst-dvb-descriptor.h

glib_enum_define=GST_MPEGTS
glib_gen_prefix=gst_mpegts
glib_gen_basename=gstmpegts

BUILT_SOURCES = gstmpegts-enumtypes.c gstmpegts-enumtypes.h
nodist_libgstmpegts_@GST_API_VERSION@_la_SOURCES = gstmpegts-enumtypes.c

CLEANFILES = $(BUILT_SOURCES)

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstMpegts-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstmpegts_@GST_API_VERSION@include_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(built_headers))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstmpegts_@GST_API_VERSION@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(built_sources))

GstMpegts-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstmpegts-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstMpegts \
		--nsversion=@GST_API_VERSION@ \
		--identifier-prefix=GstMpegts \
		--symbol-prefix=gst_mpegts \
		--symbol-prefix=gst \
		--warn-all -v \
		--c-include "gst/mpegts/mpegts.h" \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-video-@GST_API_VERSION@` \
		--library=libgstmpegts-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg gstreamer-video-@GST_API_VERSION@ \
		--pkg-export gstreamer-mpegts-@GST_API_VERSION@ \
		--add-init-section="gst_init(NULL,NULL);" \
		-DGST_USE_UNSTABLE_API \
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
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif

Android.mk:  $(BUILT_SOURCES) Makefile.am
	androgenizer -:PROJECT libgstmpegts -:STATIC libgstmpegts-@GST_API_VERSION@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmpegts_@GST_API_VERSION@_la_SOURCES) \
         $(built_sources) \
	 -:CFLAGS $(DEFS) $(libgstmpegts_@GST_API_VERSION@_la_CFLAGS) \
	 -:LDFLAGS $(libgstmpegts_@GST_API_VERSION@_la_LDFLAGS) \
	           $(libgstmpegts@GST_API_VERSION@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_API_VERSION@/gst/mpegts \
	 -:HEADERS $(libgstmpegtsinclude_HEADERS) \
         $(built_headers) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@

include $(top_srcdir)/common/gst-glib-gen.mak
