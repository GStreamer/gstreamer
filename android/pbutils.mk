LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

GST_PBUTILS_DIR := gst-libs/gst/pbutils/

gst_pbutils_COPY_HEADERS_TO := gstreamer-0.10/gst/pbutils
gst_pbutils_COPY_HEADERS_BASE := \
	gst-libs/gst/pbutils/descriptions.h \
	gst-libs/gst/pbutils/install-plugins.h \
	gst-libs/gst/pbutils/missing-plugins.h \
	gst-libs/gst/pbutils/pbutils.h \
	gst-libs/gst/pbutils/pbutils-enumtypes.h

gst_pbutils_COPY_HEADERS := $(addprefix ../,$(gst_pbutils_COPY_HEADERS_BASE))
	



LOCAL_ARM_MODE := arm

BUILT_SOURCES = \
	pbutils-enumtypes.c \
	pbutils-enumtypes.h \
	pbutils-marshal.c   \
	pbutils-marshal.h

LOCAL_SRC_FILES := \
	gstpluginsbaseversion.c \
	pbutils.c         	\
	codec-utils.c     	\
	descriptions.c    	\
	encoding-profile.c	\
	encoding-target.c	\
	install-plugins.c 	\
	missing-plugins.c 	\
	gstdiscoverer.c   	\
	gstdiscoverer-types.c 	\
	pbutils-enumtypes.c	\
	pbutils-marshal.c

LOCAL_SRC_FILES := $(addprefix ../$(GST_PBUTILS_DIR),$(LOCAL_SRC_FILES))

$(BUILT_SOURCES):
	make -C $(GST_PLUGINS_BASE_TOP)/$(GST_PBUTILS_DIR) $@


LOCAL_SHARED_LIBRARIES := \
    libgstvideo-0.10        \
    libgstreamer-0.10       \
    libgstbase-0.10         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			

LOCAL_MODULE:= libgstpbutils-0.10
LOCAL_CFLAGS := -DGSTREAMER_BUILT_FOR_ANDROID \
	$(GST_PLUGINS_BASE_CFLAGS)
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

LOCAL_COPY_HEADERS_TO := $(gst_pbutils_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_pbutils_COPY_HEADERS)
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
