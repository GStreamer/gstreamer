lib_LTLIBRARIES = libgstfft-@GST_API_VERSION@.la

libgstfft_@GST_API_VERSION@_includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/fft

libgstfft_@GST_API_VERSION@_include_HEADERS = \
	fft.h \
	gstfft.h \
	gstffts16.h \
	gstffts32.h \
	gstfftf32.h \
	gstfftf64.h

noinst_HEADERS = \
	kiss_fft_s16.h \
	kiss_fft_s32.h \
	kiss_fft_f32.h \
	kiss_fft_f64.h \
	kiss_fftr_s16.h \
	kiss_fftr_s32.h \
	kiss_fftr_f32.h \
	kiss_fftr_f64.h \
	_kiss_fft_guts_s16.h \
	_kiss_fft_guts_s32.h \
	_kiss_fft_guts_f32.h \
	_kiss_fft_guts_f64.h

libgstfft_@GST_API_VERSION@_la_SOURCES = \
	gstfft.c \
	gstffts16.c \
	gstffts32.c \
	gstfftf32.c \
	gstfftf64.c \
	kiss_fft_s16.c \
	kiss_fft_s32.c \
	kiss_fft_f32.c \
	kiss_fft_f64.c \
	kiss_fftr_s16.c \
	kiss_fftr_s32.c \
	kiss_fftr_f32.c \
	kiss_fftr_f64.c

libgstfft_@GST_API_VERSION@_la_LIBADD = $(GST_LIBS) $(LIBM)
libgstfft_@GST_API_VERSION@_la_CFLAGS =  $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstfft_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

EXTRA_DIST = kiss_version

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstFft-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstfft_@GST_API_VERSION@_include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstfft_@GST_API_VERSION@_la_SOURCES))

GstFft-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstfft-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstFft \
		--nsversion=@GST_API_VERSION@ \
		--strip-prefix=Gst \
		--warn-all \
		--c-include "gst/fft/fft.h" \
		-I$(top_srcdir)/gst-libs \
		-I$(top_builddir)/gst-libs \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--library=libgstfft-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg-export gstreamer-fft-@GST_API_VERSION@ \
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

Android.mk: Makefile.am
	androgenizer -:PROJECT libgstfft -:SHARED libgstfft-@GST_API_VERSION@ \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstfft_@GST_API_VERSION@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(libgstfft_@GST_API_VERSION@_la_CFLAGS) \
	 -:LDFLAGS $(libgstfft_@GST_API_VERSION@_la_LDFLAGS) \
	           $(libgstfft_@GST_API_VERSION@_la_LIBADD) \
	           -ldl \
	 -:HEADER_TARGET gstreamer-@GST_API_VERSION@/gst/fft \
	 -:HEADERS $(libgstfftinclude_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@
