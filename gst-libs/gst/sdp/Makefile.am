libgstsdpincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/sdp

libgstsdpinclude_HEADERS = gstsdp.h            \
			   gstsdpmessage.h

lib_LTLIBRARIES = libgstsdp-@GST_API_VERSION@.la

libgstsdp_@GST_API_VERSION@_la_SOURCES = gstsdpmessage.c

libgstsdp_@GST_API_VERSION@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
libgstsdp_@GST_API_VERSION@_la_LIBADD = $(GST_LIBS) $(GIO_LIBS)
libgstsdp_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstSdp-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstsdpinclude_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstsdp_@GST_API_VERSION@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/sdp/%',$(libgstsdpinclude_HEADERS))

GstSdp-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstsdp-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH="" GST_PLUGIN_PATH="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstSdp \
		--nsversion=@GST_API_VERSION@ \
		--strip-prefix=Gst \
		--warn-all \
		$(gir_cincludes) \
		-I$(top_srcdir)/gst-libs \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--library=libgstsdp-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg-export gstreamer-sdp-@GST_API_VERSION@ \
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

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer -:PROJECT libgstsdp -:SHARED libgstsdp-@GST_API_VERSION@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstsdp_@GST_API_VERSION@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstsdp_@GST_API_VERSION@_la_CFLAGS) \
	 -:LDFLAGS $(libgstsdp_@GST_API_VERSION@_la_LDFLAGS) \
	           $(libgstsdp_@GST_API_VERSION@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_API_VERSION@/gst/sdp \
	 -:HEADERS $(libgstsdpinclude_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
