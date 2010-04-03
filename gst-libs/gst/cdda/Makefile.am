lib_LTLIBRARIES = libgstcdda-@GST_MAJORMINOR@.la

libgstcdda_@GST_MAJORMINOR@_la_SOURCES = \
	gstcddabasesrc.c

libgstcdda_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/cdda
libgstcdda_@GST_MAJORMINOR@include_HEADERS = \
	gstcddabasesrc.h

libgstcdda_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
  $(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_MAJORMINOR@.la
libgstcdda_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstcdda_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

# Disabled until this is fixed:
# http://bugzilla.gnome.org/show_bug.cgi?id=594209
if FALSE
if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstCdda-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstcdda_@GST_MAJORMINOR@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstcdda_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/cdda/%',$(libgstcdda_@GST_MAJORMINOR@include_HEADERS))

GstCdda-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstcdda-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstCdda \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		--add-include-path=$(builddir)/../tag \
		--library=libgstcdda-0.10.la \
		--include=Gst-0.10 \
		--include=GstBase-0.10 \
		--include=GstTag-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
		--pkg gstreamer-base-0.10 \
		--pkg gstreamer-tag-0.10 \
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
		--includedir=$(builddir)/../tag \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES = $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
endif
