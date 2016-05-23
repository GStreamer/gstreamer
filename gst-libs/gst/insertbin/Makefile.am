lib_LTLIBRARIES = libgstinsertbin-@GST_API_VERSION@.la

libgstinsertbin_@GST_API_VERSION@_la_SOURCES = gstinsertbin.c

libgstinsertbin_@GST_API_VERSION@includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/insertbin

libgstinsertbin_@GST_API_VERSION@include_HEADERS = gstinsertbin.h

libgstinsertbin_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_CFLAGS)

libgstinsertbin_@GST_API_VERSION@_la_LIBADD = \
	$(GST_LIBS)

libgstinsertbin_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)


if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstInsertBin-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstinsertbin_@GST_API_VERSION@include_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(built_headers))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstinsertbin_@GST_API_VERSION@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(built_sources))

GstInsertBin-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstinsertbin-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)"  GI_SCANNER_DISABLE_CACHE=yes\
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
		CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" PKG_CONFIG="$(PKG_CONFIG)" DLLTOOL="$(DLLTOOL)" \
		$(INTROSPECTION_SCANNER) -v --namespace GstInsertBin \
		--nsversion=@GST_API_VERSION@ \
		--identifier-prefix=Gst \
		--symbol-prefix=gst \
		--warn-all \
		--c-include "gst/insertbin/gstinsertbin.h" \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
		--library=libgstinsertbin-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--include=GstBase-@GST_API_VERSION@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg gstreamer-base-@GST_API_VERSION@ \
		--pkg-export gstreamer-insertbin-@GST_API_VERSION@ \
		--add-init-section="$(INTROSPECTION_INIT)" \
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

CLEANFILES = $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
