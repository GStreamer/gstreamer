lib_LTLIBRARIES = libgstinterfaces-@GST_MAJORMINOR@.la
libgstinterfacesincludedir = \
        $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/interfaces

headers_interfaces = 		\
	colorbalance.h		\
	colorbalancechannel.h	\
	mixer.h			\
	mixeroptions.h		\
	mixertrack.h		\
	navigation.h		\
	propertyprobe.h		\
	streamvolume.h          \
	tuner.h			\
	tunernorm.h		\
	tunerchannel.h		\
	videoorientation.h	\
	xoverlay.h

# variables used for enum/marshal generation
glib_enum_headers = $(headers_interfaces)
glib_enum_define = GST_INTERFACES
glib_gen_prefix = gst_interfaces
glib_gen_basename = interfaces

built_sources = \
	interfaces-marshal.c \
	interfaces-enumtypes.c

built_headers = \
	interfaces-marshal.h \
	interfaces-enumtypes.h

libgstinterfacesinclude_HEADERS = \
	$(headers_interfaces)

nodist_libgstinterfacesinclude_HEADERS = \
	interfaces-enumtypes.h

libgstinterfaces_@GST_MAJORMINOR@_la_SOURCES = \
	colorbalance.c		\
	colorbalancechannel.c	\
	mixer.c			\
	mixeroptions.c		\
	mixertrack.c		\
	navigation.c		\
	propertyprobe.c		\
	streamvolume.c          \
	tuner.c			\
	tunernorm.c		\
	tunerchannel.c		\
	videoorientation.c	\
	xoverlay.c

nodist_libgstinterfaces_@GST_MAJORMINOR@_la_SOURCES = 	\
	$(built_sources)				\
	interfaces-marshal.h

libgstinterfaces_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstinterfaces_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) $(LIBM) 
libgstinterfaces_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

BUILT_SOURCES = \
	$(built_sources) \
	$(built_headers)

EXTRA_DIST = interfaces-marshal.list

CLEANFILES = $(BUILT_SOURCES)

include $(top_srcdir)/common/gst-glib-gen.mak

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstInterfaces-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstinterfacesinclude_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(nodist_libgstinterfacesinclude_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstinterfaces_@GST_MAJORMINOR@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(nodist_libgstinterfaces_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/interfaces/%',$(libgstinterfacesinclude_HEADERS))
gir_cincludes+=$(patsubst %,--c-include='gst/interfaces/%',$(nodist_libgstinterfacesinclude_HEADERS))

GstInterfaces-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstinterfaces-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH="" GST_PLUGIN_PATH="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstInterfaces \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_MAJORMINOR@` \
		--library=libgstinterfaces-@GST_MAJORMINOR@.la \
		--include=Gst-@GST_MAJORMINOR@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_MAJORMINOR@ \
		--pkg-export gstreamer-interfaces-@GST_MAJORMINOR@ \
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

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer -:PROJECT libgstinterfaces -:SHARED libgstinterfaces-@GST_MAJORMINOR@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstinterfaces_@GST_MAJORMINOR@_la_SOURCES) \
	           $(nodist_libgstinterfaces_@GST_MAJORMINOR@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(libgstinterfaces_@GST_MAJORMINOR@_la_CFLAGS) \
	 -:LDFLAGS $(libgstinterfaces_@GST_MAJORMINOR@_la_LDFLAGS) \
	           $(libgstinterfaces_@GST_MAJORMINOR@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_MAJORMINOR@/gst/interfaces \
	 -:HEADERS $(libgstinterfacesinclude_HEADERS) \
	           $(nodist_libgstinterfacesinclude_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
