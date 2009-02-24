include $(GNUSTEP_MAKEFILES)/common.make

SUBPROJECT_NAME = gstlibsgstgl

#gstglwindow_win32.c will be replaced by gstglwindow_cocoa.m
gstlibsgstgl_C_FILES = \
  gstglwindow_win32.c \
  gstgldisplay.c \
  gstglbuffer.c \
  gstglfilter.c \
  gstglshader.c

#gstlibsgstgl_OBJC_FILES = gstglwindow_cocoa.m

ifeq ($(GNUSTEP_TARGET_OS), mingw32)
gstlibsgstgl_INCLUDE_DIRS = \
  -I../../../win32/common \
  -ID:/workspace/glib/include/glib-2.0 \
  -ID:/workspace/gstreamer/include \
  -ID:/workspace/glib/lib/glib-2.0/include \
  -ID:/workspace/glew/include
endif

gstlibsgstgl_CFLAGS = -Wall -DHAVE_CONFIG_H

include $(GNUSTEP_MAKEFILES)/subproject.make
