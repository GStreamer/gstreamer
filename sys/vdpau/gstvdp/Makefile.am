lib_LTLIBRARIES = libgstvdp-@GST_API_VERSION@.la
 
libgstvdp_@GST_API_VERSION@_la_SOURCES = \
	gstvdpdevice.c \
	gstvdputils.c \
	gstvdpbuffer.c \
	gstvdpbufferpool.c \
	gstvdpvideobuffer.c \
	gstvdpvideobufferpool.c \
	gstvdpoutputbuffer.c \
	gstvdpoutputbufferpool.c \
	gstvdpvideosrcpad.c \
	gstvdpoutputsrcpad.c \
	gstvdpdecoder.c \
	gstvdp.c 
	
libgstvdp_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/vdpau
libgstvdp_@GST_API_VERSION@include_HEADERS = \
	gstvdpdevice.h \
	gstvdputils.h \
	gstvdpbuffer.h \
	gstvdpbufferpool.h \
	gstvdpvideobuffer.h \
	gstvdpvideobufferpool.h \
	gstvdpoutputbuffer.h \
	gstvdpoutputbufferpool.h \
	gstvdpvideosrcpad.h \
	gstvdpoutputsrcpad.h \
	gstvdpdecoder.h \
	gstvdp.h
 
libgstvdp_@GST_API_VERSION@_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(X11_CFLAGS) $(VDPAU_CFLAGS)
  
libgstvdp_@GST_API_VERSION@_la_LIBADD = $(GST_LIBS) $(X11_LIBS) $(VDPAU_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	../basevideodecoder/libgstbasevideodecoder.la
  
libgstvdp_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_LT_LDFLAGS) $(GST_ALL_LDFLAGS)
libgstvdp_@GST_API_VERSION@_la_LIBTOOLFLAGS = --tag=disable-static
