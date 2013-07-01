if USE_UVCH264
UVCH264_DIR=uvch264
else
UVCH264_DIR=
endif

if HAVE_GTK
GTK_EXAMPLES=mxf camerabin2 $(UVCH264_DIR)
else
GTK_EXAMPLES=
endif

if USE_DIRECTFB
DIRECTFB_DIR=directfb
else
DIRECTFB_DIR=
endif

OPENCV_EXAMPLES=opencv

SUBDIRS= mpegts $(DIRECTFB_DIR) $(GTK_EXAMPLES) $(OPENCV_EXAMPLES)
DIST_SUBDIRS= mpegts camerabin2 directfb mxf opencv uvch264

include $(top_srcdir)/common/parallel-subdirs.mak
