LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_pbutils_COPY_HEADERS_TO := gstreamer-0.11/gst/pbutils
gst_pbutils_COPY_HEADERS_BASE := \
	gst-libs/gst/pbutils/descriptions.h \
	gst-libs/gst/pbutils/install-plugins.h \
	gst-libs/gst/pbutils/missing-plugins.h \
	gst-libs/gst/pbutils/pbutils.h
gst_pbutils_COPY_HEADERS_ANDROID := \
	gst-libs/gst/pbutils/pbutils-enumtypes.h

gst_pbutils_COPY_HEADERS := $(addprefix ../,$(gst_pbutils_COPY_HEADERS_BASE)) \
							$(addprefix ../android/,$(gst_pbutils_COPY_HEADERS_ANDROID))
	


include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

pbutils_LOCAL_SRC_FILES_BASE:= \
   	gst-libs/gst/pbutils/pbutils.c \
   	gst-libs/gst/pbutils/descriptions.c \
   	gst-libs/gst/pbutils/install-plugins.c \
   	gst-libs/gst/pbutils/missing-plugins.c 
pbutils_LOCAL_SRC_FILES_ANDROID:= \
	gst-libs/gst/pbutils/pbutils-enumtypes.c

LOCAL_SRC_FILES:= $(addprefix ../,$(pbutils_LOCAL_SRC_FILES_BASE)) \
				  $(addprefix ../android/,$(pbutils_LOCAL_SRC_FILES_ANDROID))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.11       \
    libgstbase-0.11         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			

LOCAL_MODULE:= libgstpbutils-0.11

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst-libs/gst/pbutils 	\
    $(LOCAL_PATH)/../gst-libs            	\
    $(LOCAL_PATH)/..            		 	\
    $(LOCAL_PATH)      			 			\
	$(LOCAL_PATH)/gst-libs/gst/pbutils  	\
    $(TARGET_OUT_HEADERS)/gstreamer-0.11    \
	$(TARGET_OUT_HEADERS)/glib-2.0 			\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib     \
	external/libxml2/include


ifeq ($(STECONF_ANDROID_VERSION),"FROYO")
LOCAL_SHARED_LIBRARIES += libicuuc 
LOCAL_C_INCLUDES += external/icu4c/common
endif

LOCAL_CFLAGS := -DHAVE_CONFIG_H	-DGSTREAMER_BUILT_FOR_ANDROID
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

LOCAL_COPY_HEADERS_TO := $(gst_pbutils_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_pbutils_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
