plugin_LTLIBRARIES = libgstmsdk.la

libgstmsdk_la_SOURCES = \
	gstmsdkh264dec.c \
	gstmsdkh264enc.c \
	gstmsdkh265dec.c \
	gstmsdkh265enc.c \
	gstmsdkmjpegdec.c \
	gstmsdkmjpegenc.c \
	gstmsdkmpeg2enc.c \
	gstmsdkvp8enc.c \
	gstmsdkdec.c \
	gstmsdkenc.c \
	gstmsdk.c

# Causes linking libgstmsdk.la with CXXLINK, required by libmfx
nodist_EXTRA_libgstmsdk_la_SOURCES = not_present.cxx

noinst_HEADERS = \
	msdk.h \
	gstmsdkh264dec.h \
	gstmsdkh264enc.h \
	gstmsdkh265dec.h \
	gstmsdkh265enc.h \
	gstmsdkmjpegdec.h \
	gstmsdkmjpegenc.h \
	gstmsdkmpeg2enc.h \
	gstmsdkvp8enc.h \
	gstmsdkdec.h \
	gstmsdkenc.h

libgstmsdk_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_PBUTILS_CFLAGS) \
	$(GST_VIDEO_CFLAGS) \
	$(MSDK_CFLAGS)

libgstmsdk_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_PBUTILS_LIBS) \
	$(GST_VIDEO_LIBS) \
	$(MSDK_LIBS)

if USE_MSDK_LIBVA
libgstmsdk_la_SOURCES += \
	msdk.c \
	msdk_libva.c
libgstmsdk_la_CFLAGS += \
	$(LIBVA_DRM_CFLAGS)
libgstmsdk_la_LIBADD += \
	$(LIBVA_DRM_LIBS)
endif

libgstmsdk_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
