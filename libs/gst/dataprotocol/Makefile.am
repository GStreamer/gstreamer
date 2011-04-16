lib_LTLIBRARIES = libgstdataprotocol-@GST_MAJORMINOR@.la

libgstdataprotocol_@GST_MAJORMINOR@_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/dataprotocol
libgstdataprotocol_@GST_MAJORMINOR@_include_HEADERS = \
    dataprotocol.h
noinst_HEADERS = dp-private.h

libgstdataprotocol_@GST_MAJORMINOR@_la_SOURCES = \
    dataprotocol.c

libgstdataprotocol_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstdataprotocol_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS)
libgstdataprotocol_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

CLEANFILES = *.gcno *.gcda *.gcov *.gcov.out

%.c.gcov: .libs/libgstdataprotocol_@GST_MAJORMINOR@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstdataprotocol_@GST_MAJORMINOR@_la_SOURCES:=.gcov)

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer -:SHARED libgstdataprotocol-@GST_MAJORMINOR@ -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstdataprotocol_@GST_MAJORMINOR@_la_SOURCES) \
	 -:CFLAGS $(libgstdataprotocol_@GST_MAJORMINOR@_la_CFLAGS) \
	 -:LDFLAGS $(libgstdataprotocol_@GST_MAJORMINOR@_la_LDFLAGS) \
	           $(libgstdataprotocol_@GST_MAJORMINOR@_la_LIBADD) \
	 -:HEADER_TARGET gstreamer-@GST_MAJORMINOR@/gst/dataprotocol \
	 -:HEADERS $(libgstdataprotocol_@GST_MAJORMINOR@_include_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@

# Disabled until this is fixed:
# http://bugzilla.gnome.org/show_bug.cgi?id=594202
if FALSE
if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstDataprotocol-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstdataprotocol_@GST_MAJORMINOR@_include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstdataprotocol_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/dataprotocol/%',$(libgstdataprotocol_@GST_MAJORMINOR@_include_HEADERS))

GstDataprotocol-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstdataprotocol-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH="" GST_PLUGIN_PATH="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstDataprotocol \
		--strip-prefix=Gst \
		--nsversion=@GST_MAJORMINOR@ \
		-I$(top_srcdir) \
		-I$(top_builddir) \
		$(gir_cincludes) \
		--add-include-path=$(top_builddir)/gst \
		--library-path=$(top_builddir)/gst \
		--library=$(top_builddir)/gst/libgstreamer-0.10.la \
		--library=libgstdataprotocol-0.10.la \
		--include=Gst-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
		--pkg-export=gstreamer-dataprotocol-0.10 \
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
	$(AM_V_GEN)$(INTROSPECTION_COMPILER) --includedir=$(srcdir) --includedir=$(builddir) --includedir=$(top_builddir)/gst $(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
endif
