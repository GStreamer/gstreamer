lib_LTLIBRARIES = libgstallocators-@GST_API_VERSION@.la

libgstallocators_@GST_API_VERSION@_includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/allocators

libgstallocators_@GST_API_VERSION@_include_HEADERS = \
	allocators.h \
	gstdmabuf.h

noinst_HEADERS =

libgstallocators_@GST_API_VERSION@_la_SOURCES = \
	gstdmabuf.c 

libgstallocators_@GST_API_VERSION@_la_LIBADD = $(GST_LIBS) $(LIBM)
libgstallocators_@GST_API_VERSION@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstallocators_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstAllocators-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstallocators_@GST_API_VERSION@_include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstallocators_@GST_API_VERSION@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/allocators/%',$(libgstallocators_@GST_API_VERSION@_include_HEADERS))

GstAllocators-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstallocators-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH="" GST_PLUGIN_PATH="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstAllocators \
		--nsversion=@GST_API_VERSION@ \
		--strip-prefix=Gst \
		--warn-all \
		--c-include "gst/allocators/allocators.h" \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--library=libgstallocators-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg-export gstreamer-allocators-@GST_API_VERSION@ \
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
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES = $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif

Android.mk: Makefile.am
	androgenizer -:PROJECT libgstallocators -:SHARED libgstallocators-@GST_API_VERSION@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstallocators_@GST_API_VERSION@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(libgstallocators_@GST_API_VERSION@_la_CFLAGS) \
	 -:LDFLAGS $(libgstallocators_@GST_API_VERSION@_la_LDFLAGS) \
	           $(libgstallocators_@GST_API_VERSION@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_API_VERSION@/gst/allocators \
	 -:HEADERS $(libgstallocatorsinclude_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
