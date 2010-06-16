libgsttagincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/tag

libgsttaginclude_HEADERS = \
	tag.h gsttagdemux.h

lib_LTLIBRARIES = libgsttag-@GST_MAJORMINOR@.la

libgsttag_@GST_MAJORMINOR@_la_SOURCES = \
    gstvorbistag.c gstid3tag.c gstxmptag.c gstexiftag.c \
    lang.c tags.c gsttagdemux.c gsttageditingprivate.c
libgsttag_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgsttag_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) 
libgsttag_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

# lang-tables.dat contains generated static data and is included by lang.c
EXTRA_DIST = lang-tables.dat

noinst_HEADERS = gsttageditingprivate.h

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstTag-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgsttaginclude_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgsttag_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/audio/%',$(libgsttainclude_HEADERS))

GstTag-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgsttag-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstTag \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		--library=libgsttag-0.10.la \
		--include=Gst-0.10 \
		--include=GstBase-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
		--pkg gstreamer-base-0.10 \
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
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES = $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif

# little program that reads iso_639.xml and outputs tables for us as fallback
# for when iso-codes are not available (and so we don't have to read the xml
# just to map codes)
if USE_ISO_CODES
ISO_CODE_PROGS = mklangtables
mklangtables_SOURCES = mklangtables.c
mklangtables_CFLAGS = $(GST_CFLAGS)
mklangtables_LDADD = $(GST_LIBS)
else
ISO_CODE_PROGS =
endif

noinst_PROGRAMS = $(ISO_CODE_PROGS)
