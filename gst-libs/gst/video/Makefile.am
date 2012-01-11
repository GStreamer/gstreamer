# variables used for enum/marshal generation
glib_enum_headers = video.h
glib_enum_define = GST_VIDEO
glib_gen_prefix = gst_video
glib_gen_basename = video

lib_LTLIBRARIES = libgstvideo-@GST_MAJORMINOR@.la

ORC_SOURCE=videoblendorc
include $(top_srcdir)/common/orc.mak

built_sources = video-enumtypes.c
built_headers = video-enumtypes.h

# orc.mak sets BUILT_SOURCES as well
BUILT_SOURCES += $(built_sources) $(built_headers)

CLEANFILES = $(BUILT_SOURCES)

# video-blend.h should be disted but not installed into the includedir
libgstvideo_@GST_MAJORMINOR@_la_SOURCES = \
	video.c gstvideosink.c gstvideofilter.c convertframe.c \
	video-blend.c video-blend.h video-overlay-composition.c
nodist_libgstvideo_@GST_MAJORMINOR@_la_SOURCES = \
	$(built_sources) $(built_headers) \
	$(ORC_NODIST_SOURCES)
libgstvideo_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/video
libgstvideo_@GST_MAJORMINOR@include_HEADERS = \
	video.h gstvideosink.h gstvideofilter.h video-overlay-composition.h
nodist_libgstvideo_@GST_MAJORMINOR@include_HEADERS = $(built_headers)

libgstvideo_@GST_MAJORMINOR@_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS)\
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(ORC_CFLAGS)

libgstvideo_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(ORC_LIBS)

libgstvideo_@GST_MAJORMINOR@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)

include $(top_srcdir)/common/gst-glib-gen.mak

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstVideo-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstvideo_@GST_MAJORMINOR@include_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(built_headers))
video_gir_sources=$(patsubst %,$(srcdir)/%, $(libgstvideo_@GST_MAJORMINOR@_la_SOURCES))
video_gir_sources+=$(patsubst %,$(builddir)/%, $(built_sources))
gir_sources=$(subst $(srcdir)/video-blend.h,,$(subst $(srcdir)/video-blend.c,,$(video_gir_sources)))
gir_cincludes=$(patsubst %,--c-include='gst/video/%',$(libgstvideo_@GST_MAJORMINOR@include_HEADERS))
gir_cincludes+=$(patsubst %,--c-include='gst/video/%',$(nodist_libgstvideo_@GST_MAJORMINOR@include_HEADERS))

GstVideo-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstvideo-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH="" GST_PLUGIN_PATH="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstVideo \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_MAJORMINOR@` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_MAJORMINOR@` \
		--library=libgstvideo-@GST_MAJORMINOR@.la \
		--include=Gst-@GST_MAJORMINOR@ \
		--include=GstBase-@GST_MAJORMINOR@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_MAJORMINOR@ \
		--pkg gstreamer-base-@GST_MAJORMINOR@ \
		--pkg-export gstreamer-video-@GST_MAJORMINOR@ \
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
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_MAJORMINOR@` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_MAJORMINOR@` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer -:PROJECT libgstvideo -:SHARED libgstvideo-@GST_MAJORMINOR@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvideo_@GST_MAJORMINOR@_la_SOURCES) \
	           $(nodist_libgstvideo_@GST_MAJORMINOR@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvideo_@GST_MAJORMINOR@_la_CFLAGS) \
	 -:LDFLAGS $(libgstvideo_@GST_MAJORMINOR@_la_LDFLAGS) \
	           $(libgstvideo_@GST_MAJORMINOR@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_MAJORMINOR@/gst/tag \
	 -:HEADERS $(libgstvideo_@GST_MAJORMINOR@include_HEADERS) \
	           $(nodist_libgstvideo_@GST_MAJORMINOR@include_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
