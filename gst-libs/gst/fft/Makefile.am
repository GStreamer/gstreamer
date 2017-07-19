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
