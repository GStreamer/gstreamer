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
	gstmsdkvpp.c \
	gstmsdk.c \
	msdk-enums.c \
	gstmsdkvpputil.c \
	gstmsdkdecproputil.c

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
	gstmsdkenc.h \
	gstmsdkvpp.h \
	gstmsdkvpputil.h \
	gstmsdkdecproputil.h

# Conditional build for vp9 decoder
libgstmsdk_vp9dec_source_c = gstmsdkvp9dec.c
libgstmsdk_vp9dec_source_h = gstmsdkvp9dec.h
if USE_MSDK_VP9_DEC
libgstmsdk_la_SOURCES += $(libgstmsdk_vp9dec_source_c)
noinst_HEADERS += $(libgstmsdk_vp9dec_source_h)
endif

libgstmsdk_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PBUTILS_CFLAGS) \
	$(GST_VIDEO_CFLAGS) \
	$(GST_ALLOCATORS_CFLAGS) \
	$(MSDK_CFLAGS) \
	$(LIBMFX_CFLAGS)\
	$(G_UDEV_CFLAGS)

libgstmsdk_la_LIBADD = \
	$(GST_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PBUTILS_LIBS) \
	$(GST_VIDEO_LIBS) \
	$(GST_ALLOCATORS_LIBS) \
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
	msdk_libva.h \
	gstmsdkallocator_libva.h
endif

# Windows compilation files, used by meson
EXTRA_DIST = \
	msdk_d3d.c \
	gstmsdkallocator_d3d.c

libgstmsdk_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
