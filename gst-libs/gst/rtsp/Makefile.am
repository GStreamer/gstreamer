libgstrtspincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/rtsp

libgstrtspinclude_HEADERS = \
			    rtsp.h \
			    gstrtsp.h \
			    gstrtsptransport.h \
			    gstrtspurl.h \
			    gstrtspmessage.h    \
			    gstrtspconnection.h \
			    gstrtspdefs.h       \
			    gstrtspextension.h       \
			    gstrtsprange.h

#gstrtspextreal.h    
#gstrtspextwms.h     

lib_LTLIBRARIES = libgstrtsp-@GST_API_VERSION@.la

built_sources = gstrtsp-enumtypes.c
built_headers = gstrtsp-enumtypes.h

libgstrtsp_@GST_API_VERSION@_la_SOURCES = gstrtsptransport.c \
					 gstrtspurl.c \
					 gstrtspmessage.c    \
                                         gstrtspconnection.c \
                                         gstrtspdefs.c       \
                                         gstrtspextension.c  \
					 gstrtsprange.c

nodist_libgstrtsp_@GST_API_VERSION@_la_SOURCES = $(built_sources)
nodist_libgstrtspinclude_HEADERS = gstrtsp-enumtypes.h

#gstrtspextwms.c  
#rtspextreal.c    

libgstrtsp_@GST_API_VERSION@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
libgstrtsp_@GST_API_VERSION@_la_LIBADD = $(GST_LIBS) $(GIO_LIBS) $(LIBM)
libgstrtsp_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS) $(WIN32_LIBS)

BUILT_SOURCES = $(built_headers) $(built_sources)
CLEANFILES = $(BUILT_SOURCES)

glib_gen_prefix = __gst_rtsp
glib_enum_define = gst_rtsp
glib_enum_headers = $(libgstrtspinclude_HEADERS)
glib_gen_basename = gstrtsp

include $(top_srcdir)/common/gst-glib-gen.mak

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstRtsp-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstrtspinclude_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(nodist_libgstrtspinclude_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstrtsp_@GST_API_VERSION@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(nodist_libgstrtsp_@GST_API_VERSION@_la_SOURCES))

GstRtsp-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstrtsp-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" GI_SCANNER_DISABLE_CACHE=yes\
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
		CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" PKG_CONFIG="$(PKG_CONFIG)" DLLTOOL="$(DLLTOOL)" \
		$(INTROSPECTION_SCANNER) -v --namespace GstRtsp \
		--nsversion=@GST_API_VERSION@ \
		--identifier-prefix=Gst \
		--symbol-prefix=gst \
		--warn-all \
		--c-include "gst/rtsp/rtsp.h" \
		-I$(top_builddir)/gst-libs \
		-I$(top_srcdir)/gst-libs \
		--add-include-path=$(builddir)/../sdp \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--library=libgstrtsp-@GST_API_VERSION@.la \
		--include=Gio-2.0 \
		--include=Gst-@GST_API_VERSION@ \
		--include=GstSdp-@GST_API_VERSION@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gio-2.0 \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg gstreamer-sdp-@GST_API_VERSION@ \
		--pkg-export gstreamer-rtsp-@GST_API_VERSION@ \
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
		--includedir=$(builddir)/../sdp \
		--includedir=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
