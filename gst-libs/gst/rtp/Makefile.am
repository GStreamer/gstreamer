libgstrtpincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/rtp

libgstrtpinclude_HEADERS = gstrtpbuffer.h \
			   gstrtcpbuffer.h \
			   gstrtppayloads.h \
			gstbasertpaudiopayload.h \
			gstbasertppayload.h \
			gstbasertpdepayload.h

lib_LTLIBRARIES = libgstrtp-@GST_MAJORMINOR@.la

libgstrtp_@GST_MAJORMINOR@_la_SOURCES = gstrtpbuffer.c \
			        gstrtcpbuffer.c \
			        gstrtppayloads.c \
			        gstbasertpaudiopayload.c \
  				gstbasertppayload.c \
  				gstbasertpdepayload.c

libgstrtp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtp_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstrtp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstRtp-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstrtpinclude_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstrtp_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/rtp/%',$(libgstrtpinclude_HEADERS))

GstRtp-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstrtp-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstRtp \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		-I$(top_srcdir)/gst-libs \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		--library=libgstrtp-0.10.la \
		--include=Gst-0.10 \
		--include=GstBase-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
		--pkg gstreamer-base-0.10 \
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
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-0.10` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES = $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
