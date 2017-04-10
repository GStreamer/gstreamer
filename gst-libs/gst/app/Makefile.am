lib_LTLIBRARIES = libgstapp-@GST_API_VERSION@.la

glib_enum_define = GST_APP
glib_gen_prefix = __gst_app
glib_gen_basename = app

glib_enum_headers = gstappsrc.h

built_sources = app-enumtypes.c
built_headers = app-enumtypes.h
BUILT_SOURCES = $(built_sources) $(built_headers)

include $(top_srcdir)/common/gst-glib-gen.mak

libgstapp_@GST_API_VERSION@_la_SOURCES = gstappsrc.c gstappsink.c
nodist_libgstapp_@GST_API_VERSION@_la_SOURCES = $(BUILT_SOURCES)
libgstapp_@GST_API_VERSION@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstapp_@GST_API_VERSION@_la_LIBADD = $(GST_BASE_LIBS)
libgstapp_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstapp_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/app
libgstapp_@GST_API_VERSION@include_HEADERS = \
	app.h \
	gstappsrc.h \
	gstappsink.h
nodist_libgstapp_@GST_API_VERSION@include_HEADERS = app-enumtypes.h

CLEANFILES = $(BUILT_SOURCES)

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstApp-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstapp_@GST_API_VERSION@include_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(nodist_libgstapp_@GST_API_VERSION@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstapp_@GST_API_VERSION@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(nodist_libgstapp_@GST_API_VERSION@_la_SOURCES))

GstApp-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstapp-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" GI_SCANNER_DISABLE_CACHE=yes\
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
		CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" PKG_CONFIG="$(PKG_CONFIG)" DLLTOOL="$(DLLTOOL)" \
		$(INTROSPECTION_SCANNER) -v --namespace GstApp \
		--nsversion=@GST_API_VERSION@ \
		--identifier-prefix=Gst \
		--symbol-prefix=gst \
		--warn-all \
		--c-include "gst/app/app.h" \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
		--library=libgstapp-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--include=GstBase-@GST_API_VERSION@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg gstreamer-base-@GST_API_VERSION@ \
		--pkg-export gstreamer-app-@GST_API_VERSION@ \
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
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
