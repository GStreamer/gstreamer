include $(GNUSTEP_MAKEFILES)/common.make

SUBPROJECT_NAME = gstgleffects

gstgleffects_C_FILES = \
  gstgleffectssources.c \
  gstgleffectidentity.c \
  gstgleffectmirror.c \
  gstgleffectsqueeze.c \
  gstgleffectstretch.c \
  gstgleffecttunnel.c \
  gstgleffectfisheye.c \
  gstgleffecttwirl.c \
  gstgleffectbulge.c \
  gstgleffectsquare.c \
  gstgleffectlumatocurve.c \
  gstgleffectrgbtocurve.c \
  gstgleffectsin.c \
  gstgleffectglow.c \
  gstgleffectxray.c

gstgleffects_INCLUDE_DIRS = \
  -I.. \
  -I../../../gst-libs/gst/gl

ifeq ($(GNUSTEP_TARGET_OS), mingw32)
  gstgleffects_INCLUDE_DIRS += \
    -I../../../win32/common \
    -ID:/workspace/glib/include/glib-2.0 \
    -ID:/workspace/gstreamer/include \
    -ID:/workspace/glib/lib/glib-2.0/include \
    -ID:/workspace/glew/include
endif

gstgleffects_CFLAGS = -Wall -DHAVE_CONFIG_H

include $(GNUSTEP_MAKEFILES)/subproject.make
