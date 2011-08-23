if HAVE_GTK
GTK_EXAMPLES=camerabin mxf scaletempo camerabin2
else
GTK_EXAMPLES=
endif

if USE_DIRECTFB
DIRECTFB_DIR=directfb
else
DIRECTFB_DIR=
endif

OPENCV_EXAMPLES=opencv

SUBDIRS= $(DIRECTFB_DIR) $(GTK_EXAMPLES) $(OPENCV_EXAMPLES)
DIST_SUBDIRS= camerabin camerabin2 directfb mxf scaletempo opencv

include $(top_srcdir)/common/parallel-subdirs.mak
