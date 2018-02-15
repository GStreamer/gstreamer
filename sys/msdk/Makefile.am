plugin_LTLIBRARIES = libgstmsdk.la

libgstmsdk_la_SOURCES = \
	gstmsdkcontext.c \
	gstmsdkcontextutil.c \
	gstmsdksystemmemory.c \
	gstmsdkvideomemory.c \
	gstmsdkbufferpool.c \
	gstmsdkh264dec.c \
	gstmsdkh264enc.c \
	gstmsdkh265dec.c \
	gstmsdkh265enc.c \
	gstmsdkmjpegdec.c \
	gstmsdkmjpegenc.c \
	gstmsdkmpeg2dec.c \
	gstmsdkmpeg2enc.c \
	gstmsdkvp8dec.c \
	gstmsdkvp8enc.c \
	gstmsdkvc1dec.c \
	gstmsdkdec.c \
	gstmsdkenc.c \
	gstmsdk.c \
        msdk-enums.c

# Causes linking libgstmsdk.la with CXXLINK, required by libmfx
nodist_EXTRA_libgstmsdk_la_SOURCES = not_present.cxx

noinst_HEADERS = \
	msdk.h \
        msdk-enums.h \
	gstmsdkcontext.h \
	gstmsdkcontextutil.h \
	gstmsdksystemmemory.h \
	gstmsdkvideomemory.h \
	gstmsdkallocator.h \
	gstmsdkbufferpool.h \
	gstmsdkh264dec.h \
	gstmsdkh264enc.h \
	gstmsdkh265dec.h \
	gstmsdkh265enc.h \
	gstmsdkmjpegdec.h \
	gstmsdkmjpegenc.h \
	gstmsdkmpeg2dec.h \
	gstmsdkmpeg2enc.h \
	gstmsdkvp8dec.h \
	gstmsdkvp8enc.h \
	gstmsdkvc1dec.h \
	gstmsdkdec.h \
	gstmsdkenc.h

libgstmsdk_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PBUTILS_CFLAGS) \
	$(GST_VIDEO_CFLAGS) \
	$(MSDK_CFLAGS) \
	$(LIBMFX_CFLAGS)\
	$(G_UDEV_CFLAGS)

libgstmsdk_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PBUTILS_LIBS) \
	$(GST_VIDEO_LIBS) \
	$(MSDK_LIBS) \
	$(LIBMFX_LIBS) \
	$(G_UDEV_LIBS)

if USE_MSDK_LIBVA
libgstmsdk_la_SOURCES += \
	msdk.c \
	msdk_libva.c \
	gstmsdkallocator_libva.c
libgstmsdk_la_CFLAGS += \
	$(LIBVA_DRM_CFLAGS)
libgstmsdk_la_LIBADD += \
	$(LIBVA_DRM_LIBS)
noinst_HEADERS += \
	msdk_libva.h
endif

# TODO: add support for windows compilation
EXTRA_DIST = \
	msdk_d3d.c

libgstmsdk_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
