lib_LTLIBRARIES = libgstvdp-@GST_MAJORMINOR@.la
 
libgstvdp_@GST_MAJORMINOR@_la_SOURCES = \
	gstvdpdevice.c \
	gstvdputils.c \
	gstvdpvideobuffer.c \
	gstvdpoutputbuffer.c \
	gstvdpvideosrcpad.c \
	gstvdpoutputsrcpad.c \
	gstvdp.c 
	
libgstvdp_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/vdpau
libgstvdp_@GST_MAJORMINOR@include_HEADERS = \
	gstvdpdevice.h \
	gstvdpvideobuffer.h \
	gstvdpoutputbuffer.h \
	gstvdpvideosrcpad.h \
	gstvdpoutputsrcpad.h \
	gstvdp.h
 
libgstvdp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(X11_CFLAGS) $(VDPAU_CFLAGS)
  
libgstvdp_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) $(X11_LIBS) $(VDPAU_LIBS) \
	-lgstvideo-$(GST_MAJORMINOR)
  
libgstvdp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_LT_LDFLAGS) $(GST_ALL_LDFLAGS)
libgstvdp_@GST_MAJORMINOR@_la_LIBTOOLFLAGS = --tag=disable-static