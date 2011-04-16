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
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH="" GST_PLUGIN_PATH="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstNetbuffer \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_MAJORMINOR@` \
		--library=libgstnetbuffer-@GST_MAJORMINOR@.la \
		--include=Gst-@GST_MAJORMINOR@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_MAJORMINOR@ \
		--pkg-export gstreamer-netbuffer-@GST_MAJORMINOR@ \
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
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES = $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer -:PROJECT libgstnetbuffer -:SHARED libgstnetbuffer-@GST_MAJORMINOR@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstnetbuffer_@GST_MAJORMINOR@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstnetbuffer_@GST_MAJORMINOR@_la_CFLAGS) \
	 -:LDFLAGS $(libgstnetbuffer_@GST_MAJORMINOR@_la_LDFLAGS) \
	           $(libgstnetbuffer_@GST_MAJORMINOR@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_MAJORMINOR@/gst/netbuffer \
	 -:HEADERS $(libgstnetbuffer_include_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
