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
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstInterfaces \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--library=libgstinterfaces-0.10.la \
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
