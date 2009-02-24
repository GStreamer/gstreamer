include $(GNUSTEP_MAKEFILES)/common.make

SUBPROJECT_NAME = gstgl

gstgl_C_FILES += \
  gstopengl.c \
  gstglimagesink.c \
  gstglupload.c \
  gstgldownload.c \
  gstglfilterblur.c \
  gstglfiltercube.c \
  gstglfilterlaplacian.c \
  gstglfiltersobel.c \
  gstglfilterglass.c \
  gstglfilterapp.c \
  gstglcolorscale.c \
  gltestsrc.c \
  gstgltestsrc.c \
  gstgloverlay.c \
  gstgldifferencematte.c \
  gstglbumper.c \
  gstgleffects.c

gstgl_INCLUDE_DIRS += \
  -I../../gst-libs \
  -I../../gst-libs/gst/gl \
  -Ieffects

ifeq ($(GNUSTEP_TARGET_OS), mingw32)
  gstgl_INCLUDE_DIRS += \
    -I../../win32/common \
    -ID:/workspace/glib/include/glib-2.0 \
    -ID:/workspace/gstreamer/include \
    -ID:/workspace/glib/lib/glib-2.0/include \
    -ID:/workspace/png/include \
    -ID:/workspace/z/include \
    -ID:/workspace/glew/include
endif

gstgl_CFLAGS = -Wall -DHAVE_CONFIG_H

include $(GNUSTEP_MAKEFILES)/subproject.make
