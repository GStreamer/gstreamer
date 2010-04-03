libgstrtspincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/rtsp

libgstrtspinclude_HEADERS = gstrtspbase64.h \
			    gstrtsptransport.h \
			    gstrtspurl.h \
			    gstrtspmessage.h    \
			    gstrtspconnection.h \
			    gstrtspdefs.h       \
			    gstrtspextension.h       \
			    gstrtsprange.h

#gstrtspextreal.h    
#gstrtspextwms.h     

lib_LTLIBRARIES = libgstrtsp-@GST_MAJORMINOR@.la

built_sources = gstrtsp-marshal.c gstrtsp-enumtypes.c
built_headers = gstrtsp-marshal.h gstrtsp-enumtypes.h

libgstrtsp_@GST_MAJORMINOR@_la_SOURCES = gstrtspbase64.c \
					 gstrtsptransport.c \
					 gstrtspurl.c \
					 gstrtspmessage.c    \
                                         gstrtspconnection.c \
                                         gstrtspdefs.c       \
                                         gstrtspextension.c  \
					 gstrtsprange.c

nodist_libgstrtsp_@GST_MAJORMINOR@_la_SOURCES = $(built_sources)
nodist_libgstrtspinclude_HEADERS = gstrtsp-enumtypes.h

noinst_HEADERS = gstrtsp.h

#gstrtspextwms.c  
#rtspextreal.c    

libgstrtsp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtsp_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) $(WIN32_LIBS) $(HSTRERROR_LIBS)
libgstrtsp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

BUILT_SOURCES = $(built_headers) $(built_sources)
EXTRA_DIST = gstrtsp-marshal.list
CLEANFILES = $(BUILT_SOURCES)

glib_gen_prefix = gst_rtsp
glib_enum_define = gst_rtsp
glib_enum_headers = gstrtspdefs.h
glib_gen_basename = gstrtsp

include $(top_srcdir)/common/gst-glib-gen.mak

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstRtsp-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstrtspinclude_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(nodist_libgstrtspinclude_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstrtsp_@GST_MAJORMINOR@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(nodist_libgstrtsp_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/audio/%',$(libgstrtspinclude_HEADERS))
gir_cincludes+=$(patsubst %,--c-include='gst/audio/%',$(nodist_libgstrtspinclude_HEADERS))

GstRtsp-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstrtsp-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstRtsp \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		-I$(top_builddir)/gst-libs \
		-I$(top_srcdir)/gst-libs \
		--add-include-path=$(builddir)/../sdp \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--library=libgstrtsp-0.10.la \
		--include=Gst-0.10 \
		--include=GstSdp-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
		--pkg gstreamer-sdp-0.10 \
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
		--includedir=$(builddir)/../sdp \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
