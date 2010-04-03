libgstsdpincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/sdp

libgstsdpinclude_HEADERS = gstsdp.h            \
			   gstsdpmessage.h

lib_LTLIBRARIES = libgstsdp-@GST_MAJORMINOR@.la

libgstsdp_@GST_MAJORMINOR@_la_SOURCES = gstsdpmessage.c

libgstsdp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstsdp_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) $(WIN32_LIBS)
libgstsdp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstSdp-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstsdpinclude_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstsdp_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/audio/%',$(libgstsdpinclude_HEADERS))

GstSdp-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstsdp-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstSdp \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		-I$(top_srcdir)/gst-libs \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--library=libgstsdp-0.10.la \
		--include=Gst-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
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
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES = $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
