if USE_GST_V4L
V4L_DIR=v4l
else
V4L_DIR=
endif

if USE_X
XIMAGE_DIR=ximage
else
XIMAGE_DIR=
endif

if USE_XVIDEO
XVIMAGE_DIR=xvimage
else
XVIMAGE_DIR=
endif

SUBDIRS = \
  $(XIMAGE_DIR) \
  $(XVIMAGE_DIR) \
  $(V4L_DIR)

DIST_SUBDIRS = \
  v4l \
  ximage \
  xvimage

include $(top_srcdir)/common/parallel-subdirs.mak
