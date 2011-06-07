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
  $(XVIMAGE_DIR)

DIST_SUBDIRS = \
  ximage \
  xvimage

include $(top_srcdir)/common/parallel-subdirs.mak
