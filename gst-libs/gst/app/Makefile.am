lib_LTLIBRARIES = libgstapp-@GST_MAJORMINOR@.la

glib_enum_define = GST_APP
glib_gen_prefix = __gst_app
glib_gen_basename = gstapp

include $(top_srcdir)/common/gst-glib-gen.mak

built_sources = gstapp-marshal.c
built_headers = gstapp-marshal.h

BUILT_SOURCES = $(built_sources) $(built_headers)

nodist_libgstapp_@GST_MAJORMINOR@_la_SOURCES = \
             $(built_sources)

libgstapp_@GST_MAJORMINOR@_la_SOURCES = gstappsrc.c gstappbuffer.c gstappsink.c 
libgstapp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstapp_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS)
libgstapp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)
libgstapp_@GST_MAJORMINOR@_la_LIBTOOLFLAGS = --tag=disable-static

libgstapp_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/app
libgstapp_@GST_MAJORMINOR@include_HEADERS = \
	gstappsrc.h \
	gstappbuffer.h \
	gstappsink.h

CLEANFILES = $(BUILT_SOURCES)

EXTRA_DIST = gstapp-marshal.list

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstApp-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstapp_@GST_MAJORMINOR@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstapp_@GST_MAJORMINOR@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(nodist_libgstapp_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/app/%',$(libgstapp_@GST_MAJORMINOR@include_HEADERS))

GstApp-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstapp-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH="" GST_PLUGIN_PATH="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstApp \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_MAJORMINOR@` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_MAJORMINOR@` \
		--library=libgstapp-@GST_MAJORMINOR@.la \
		--include=Gst-@GST_MAJORMINOR@ \
		--include=GstBase-@GST_MAJORMINOR@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_MAJORMINOR@ \
		--pkg gstreamer-base-@GST_MAJORMINOR@ \
		--pkg-export gstreamer-app-@GST_MAJORMINOR@ \
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
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_MAJORMINOR@` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer -:PROJECT libgstapp -:SHARED libgstapp-@GST_MAJORMINOR@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstapp_@GST_MAJORMINOR@_la_SOURCES) \
	           $(nodist_libgstapp_@GST_MAJORMINOR@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstapp_@GST_MAJORMINOR@_la_CFLAGS) \
	 -:LDFLAGS $(libgstapp_@GST_MAJORMINOR@_la_LDFLAGS) \
	           $(libgstapp_@GST_MAJORMINOR@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_MAJORMINOR@/gst/app \
	 -:HEADERS $(libgstapp_@GST_MAJORMINOR@include_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
