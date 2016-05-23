lib_LTLIBRARIES = libgstplayer-@GST_API_VERSION@.la

libgstplayer_@GST_API_VERSION@_la_SOURCES = \
	gstplayer.c  \
	gstplayer-signal-dispatcher.c  \
	gstplayer-video-renderer.c  \
	gstplayer-media-info.c \
	gstplayer-g-main-context-signal-dispatcher.c  \
	gstplayer-video-overlay-video-renderer.c \
	gstplayer-visualization.c

libgstplayer_@GST_API_VERSION@_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstplayer_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)

libgstplayer_@GST_API_VERSION@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	-lgstaudio-$(GST_API_VERSION) \
	-lgsttag-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GST_LIBS) \
	$(LIBM)

libgstplayerdir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/player

noinst_HEADERS = \
	gstplayer-signal-dispatcher-private.h \
	gstplayer-video-renderer-private.h \
	gstplayer-media-info-private.h

libgstplayer_HEADERS = \
	player.h \
	gstplayer.h \
	gstplayer-types.h \
	gstplayer-signal-dispatcher.h \
	gstplayer-video-renderer.h \
	gstplayer-media-info.h \
	gstplayer-g-main-context-signal-dispatcher.h  \
	gstplayer-video-overlay-video-renderer.h \
	gstplayer-visualization.h

CLEANFILES =

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstPlayer-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstplayer_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstplayer_@GST_API_VERSION@_la_SOURCES))

GstPlayer-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstplayer-@GST_API_VERSION@.la
	$(AM_V_GEN)GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no GI_SCANNER_DISABLE_CACHE=yes \
		CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" PKG_CONFIG="$(PKG_CONFIG)" DLLTOOL="$(DLLTOOL)" \
		PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		$(INTROSPECTION_SCANNER) -v --namespace GstPlayer \
		--nsversion=@GST_API_VERSION@ \
		--warn-all \
		--strip-prefix=Gst \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--c-include "gst/player/player.h" \
		--add-include-path=$(top_builddir)/gst-libs \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-1.0` \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-1.0` \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-video-1.0` \
		--library-path=$(top_builddir)/gst-libs \
		--library=libgstplayer-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--include=GstVideo-@GST_API_VERSION@ \
		--libtool="${LIBTOOL}" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg gstreamer-audio-@GST_API_VERSION@ \
		--pkg gstreamer-video-@GST_API_VERSION@ \
		--pkg gstreamer-tag-@GST_API_VERSION@ \
		--pkg gstreamer-pbutils-@GST_API_VERSION@ \
		--pkg-export gstreamer-player-@GST_API_VERSION@ \
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
		--includedir=$(srcdir)/gst-libs \
		--includedir=$(builddir)/gst-libs \
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-1.0` \
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-1.0` \
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-video-1.0` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
