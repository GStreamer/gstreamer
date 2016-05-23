lib_LTLIBRARIES = libgstnet-@GST_API_VERSION@.la

libgstnet_@GST_API_VERSION@_includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/net
libgstnet_@GST_API_VERSION@_include_HEADERS = \
    net.h \
    gstnet.h \
    gstnetaddressmeta.h \
    gstnetclientclock.h \
    gstnetcontrolmessagemeta.h \
    gstnettimepacket.h \
    gstnettimeprovider.h \
    gstptpclock.h

libgstnet_@GST_API_VERSION@_la_SOURCES = \
    gstnetaddressmeta.c \
    gstnetclientclock.c \
    gstnetcontrolmessagemeta.c \
    gstnettimepacket.c \
    gstnettimeprovider.c \
    gstptpclock.c \
    gstntppacket.c

noinst_HEADERS = gstptp_private.h gstntppacket.h

libgstnet_@GST_API_VERSION@_la_CFLAGS = $(GST_OBJ_CFLAGS) $(GIO_CFLAGS)
libgstnet_@GST_API_VERSION@_la_LIBADD = $(GST_OBJ_LIBS) $(GIO_LIBS) \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_API_VERSION@.la
libgstnet_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

# try to prevent packaging errors
check-libexecdir-consistency:
	@if test "${GST_PTP_HELPER_INSTALLED}" != "${libexecdir}/gstreamer-$(GST_API_VERSION)/gst-ptp-helper"; then \
	  echo "*** Inconsistent libexecdir! Please use ./configure --libexecdir=/foo/bar"; \
	  echo "*** to set the libexecdir and not make libexecdir=/foo/bar or the like."; \
	  echo "*** The same goes for prefix, libdir etc."; \
	  echo "*** ${GST_PTP_HELPER_INSTALLED} != ${libexecdir}/gstreamer-$(GST_API_VERSION)/gst-ptp-helper"; \
	  exit 1; \
	fi

all-local: check-libexecdir-consistency

CLEANFILES = *.gcno *.gcda *.gcov

%.c.gcov: .libs/libgstnet_@GST_API_VERSION@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstnet_@GST_API_VERSION@_la_SOURCES:=.gcov)


if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstNet-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstnet_@GST_API_VERSION@_include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstnet_@GST_API_VERSION@_la_SOURCES))
gir_cincludes=--c-include="gst/net/net.h"

GstNet-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstnet-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" GI_SCANNER_DISABLE_CACHE=yes\
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
		CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" PKG_CONFIG="$(PKG_CONFIG)" DLLTOOL="$(DLLTOOL)" \
		$(INTROSPECTION_SCANNER) -v --namespace GstNet \
		--identifier-prefix=Gst \
		--symbol-prefix=gst \
		--warn-all \
		--nsversion=@GST_API_VERSION@ \
		-I$(top_srcdir) \
		-I$(top_srcdir)/libs \
		-I$(top_builddir) \
		-I$(top_builddir)/libs \
		$(gir_cincludes) \
		--add-include-path=$(top_builddir)/gst \
		--library-path=$(top_builddir)/gst \
		--library=libgstnet-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--include=Gio-2.0 \
		--libtool="${LIBTOOL}" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg gio-2.0 \
		--pkg-export="gstreamer-net-@GST_API_VERSION@" \
		--add-init-section="$(INTROSPECTION_INIT)" \
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
