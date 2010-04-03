libgstnetbufferincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/netbuffer

libgstnetbufferinclude_HEADERS = gstnetbuffer.h

lib_LTLIBRARIES = libgstnetbuffer-@GST_MAJORMINOR@.la

libgstnetbuffer_@GST_MAJORMINOR@_la_SOURCES = gstnetbuffer.c
libgstnetbuffer_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS)
libgstnetbuffer_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstnetbuffer_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstNetbuffer-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstnetbufferinclude_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstnetbuffer_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/netbuffer/%',$(libgstnetbufferinclude_HEADERS))

GstNetbuffer-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstnetbuffer-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstNetbuffer \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--library=libgstnetbuffer-0.10.la \
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
