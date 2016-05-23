libgstrtpincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/rtp

libgstrtpinclude_HEADERS = \
			   rtp.h \
			   gstrtpdefs.h \
			   gstrtpbuffer.h \
			   gstrtcpbuffer.h \
			   gstrtppayloads.h \
			   gstrtphdrext.h \
			gstrtpbaseaudiopayload.h \
			gstrtpbasepayload.h \
			gstrtpbasedepayload.h

lib_LTLIBRARIES = libgstrtp-@GST_API_VERSION@.la

libgstrtp_@GST_API_VERSION@_la_SOURCES = gstrtpbuffer.c \
			        gstrtcpbuffer.c \
			        gstrtppayloads.c \
			   	gstrtphdrext.c \
			        gstrtpbaseaudiopayload.c \
				gstrtpbasepayload.c \
				gstrtpbasedepayload.c

built_sources = gstrtp-enumtypes.c
built_headers = gstrtp-enumtypes.h

libgstrtp_@GST_API_VERSION@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtp_@GST_API_VERSION@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstrtp_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

nodist_libgstrtp_@GST_API_VERSION@_la_SOURCES = $(built_sources)
nodist_libgstrtpinclude_HEADERS = gstrtp-enumtypes.h

BUILT_SOURCES = $(built_headers) $(built_sources)
CLEANFILES = $(BUILT_SOURCES)

glib_gen_prefix = __gst_rtp
glib_enum_define = gst_rtp
glib_enum_headers = $(libgstrtpinclude_HEADERS)
glib_gen_basename = gstrtp

include $(top_srcdir)/common/gst-glib-gen.mak

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstRtp-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstrtpinclude_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(nodist_libgstrtpinclude_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstrtp_@GST_API_VERSION@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(nodist_libgstrtp_@GST_API_VERSION@_la_SOURCES))

GstRtp-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstrtp-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" GI_SCANNER_DISABLE_CACHE=yes\
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
		CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" PKG_CONFIG="$(PKG_CONFIG)" DLLTOOL="$(DLLTOOL)" \
		$(INTROSPECTION_SCANNER) -v --namespace GstRtp \
		--nsversion=@GST_API_VERSION@ \
		--identifier-prefix=Gst \
		--symbol-prefix=gst \
		--warn-all \
		--c-include "gst/rtp/rtp.h" \
		-I$(top_builddir)/gst-libs \
		-I$(top_srcdir)/gst-libs \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
		--library=libgstrtp-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--include=GstBase-@GST_API_VERSION@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg gstreamer-base-@GST_API_VERSION@ \
		--pkg-export gstreamer-rtp-@GST_API_VERSION@ \
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
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		$(INTROSPECTION_COMPILER) \
		--includedir=$(srcdir) \
		--includedir=$(builddir) \
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
