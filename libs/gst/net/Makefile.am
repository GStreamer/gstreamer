lib_LTLIBRARIES = libgstnet-@GST_MAJORMINOR@.la

libgstnet_@GST_MAJORMINOR@_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/net
libgstnet_@GST_MAJORMINOR@_include_HEADERS = \
    gstnet.h \
    gstnetclientclock.h \
    gstnettimepacket.h \
    gstnettimeprovider.h

libgstnet_@GST_MAJORMINOR@_la_SOURCES = \
    gstnetclientclock.c \
    gstnettimepacket.c \
    gstnettimeprovider.c

libgstnet_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstnet_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS) $(INET_ATON_LIBS) $(WIN32_LIBS)
libgstnet_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

CLEANFILES = *.gcno *.gcda *.gcov

%.c.gcov: .libs/libgstnet_@GST_MAJORMINOR@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstnet_@GST_MAJORMINOR@_la_SOURCES:=.gcov)

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer -:SHARED libgstnet-@GST_MAJORMINOR@ -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstnet_@GST_MAJORMINOR@_la_SOURCES) \
	 -:CFLAGS $(libgstnet_@GST_MAJORMINOR@_la_CFLAGS) \
	 -:LDFLAGS $(libgstnet_@GST_MAJORMINOR@_la_LDFLAGS) \
	           $(libgstnet_@GST_MAJORMINOR@_la_LIBADD) \
	 -:HEADER_TARGET gstreamer-@GST_MAJORMINOR@/gst/net \
	 -:HEADERS $(libgstnet_@GST_MAJORMINOR@_include_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@


if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstNet-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstnet_@GST_MAJORMINOR@_include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstnet_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=--c-include="gst/net/gstnet.h"

GstNet-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstnet-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH="" GST_PLUGIN_PATH="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstNet \
		--strip-prefix=Gst \
		--nsversion=@GST_MAJORMINOR@ \
		-I$(top_srcdir) \
		-I$(top_srcdir)/libs \
		-I$(top_builddir) \
		-I$(top_builddir)/libs \
		$(gir_cincludes) \
		--add-include-path=$(top_builddir)/gst \
		--library-path=$(top_builddir)/gst \
		--library=$(top_builddir)/gst/libgstreamer-0.10.la \
		--library=libgstnet-0.10.la \
		--include=Gst-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
		--pkg-export="gstreamer-net-0.10" \
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
