lib_LTLIBRARIES = libgstpbutils-@GST_MAJORMINOR@.la

headers_pbutils = \
	pbutils.h         \
	descriptions.h    \
	install-plugins.h \
	missing-plugins.h

# variables used for enum/marshal generation
glib_enum_headers = $(headers_pbutils)
glib_enum_define = GST_INSTALL
glib_gen_prefix = gst_install
glib_gen_basename = pbutils

built_sources = \
	pbutils-enumtypes.c

built_headers = \
	pbutils-enumtypes.h

libgstpbutils_@GST_MAJORMINOR@_la_SOURCES = \
	pbutils.c         \
	descriptions.c    \
	install-plugins.c \
	missing-plugins.c

nodist_libgstpbutils_@GST_MAJORMINOR@_la_SOURCES = \
	$(built_sources)

libgstpbutils_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/pbutils
libgstpbutils_@GST_MAJORMINOR@include_HEADERS = \
	$(headers_pbutils)
nodist_libgstpbutils_@GST_MAJORMINOR@include_HEADERS = \
	pbutils-enumtypes.h

libgstpbutils_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstpbutils_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstpbutils_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

BUILT_SOURCES = \
	$(built_sources) \
	$(built_headers)

CLEANFILES = $(BUILT_SOURCES)


include $(top_srcdir)/common/gst-glib-gen.mak

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstPbutils-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstpbutils_@GST_MAJORMINOR@include_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(nodist_libgstpbutils_@GST_MAJORMINOR@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstpbutils_@GST_MAJORMINOR@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(nodist_libgstpbutils_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/pbutils/%',$(libgstpbutils_@GST_MAJORMINOR@include_HEADERS))
gir_cincludes+=$(patsubst %,--c-include='gst/pbutils/%',$(nodist_libgstpbutils_@GST_MAJORMINOR@include_HEADERS))

GstPbutils-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstpbutils-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstPbutils \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--library=libgstpbutils-0.10.la \
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

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
