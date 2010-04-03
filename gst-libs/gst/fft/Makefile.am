lib_LTLIBRARIES = libgstfft-@GST_MAJORMINOR@.la

libgstfft_@GST_MAJORMINOR@_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/fft

libgstfft_@GST_MAJORMINOR@_include_HEADERS = \
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

libgstfft_@GST_MAJORMINOR@_la_SOURCES = \
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

libgstfft_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) $(LIBM)
libgstfft_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS)
libgstfft_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

EXTRA_DIST = kiss_version

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstFft-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstfft_@GST_MAJORMINOR@_include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstfft_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/fft/%',$(libgstfft_@GST_MAJORMINOR@_include_HEADERS))

GstFft-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstfft-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(PKG_CONFIG_PATH):$(top_builddir)/pkgconfig" \
		$(INTROSPECTION_SCANNER) -v --namespace GstFft \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		$(gir_cincludes) \
		--add-include-path=`$(PKG_CONFIG) --variable=girdir gstreamer-0.10` \
		--library=libgstfft-0.10.la \
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

CLEANFILES = $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
