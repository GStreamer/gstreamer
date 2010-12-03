LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_video_COPY_HEADERS_TO := gstreamer-0.10/gst/video
gst_video_COPY_HEADERS_BASE := \
	gst-libs/gst/video/gstvideofilter.h \
	gst-libs/gst/video/gstvideosink.h \
	gst-libs/gst/video/video.h
gst_video_COPY_HEADERS_ANDROID := \
	gst-libs/gst/video/video-enumtypes.h
	
gst_video_COPY_HEADERS := $(addprefix ../,$(gst_video_COPY_HEADERS_BASE)) \
						  $(addprefix ../android/,$(gst_video_COPY_HEADERS_ANDROID))
            

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

video_LOCAL_SRC_FILES_BASE:= \
 	gst-libs/gst/video/video.c \
   	gst-libs/gst/video/gstvideosink.c \
   	gst-libs/gst/video/gstvideofilter.c   	
video_LOCAL_SRC_FILES_ANDROID:= \
   	gst-libs/gst/video/video-enumtypes.c
   
LOCAL_SRC_FILES:= $(addprefix ../,$(video_LOCAL_SRC_FILES_BASE)) \
				  $(addprefix ../android/,$(video_LOCAL_SRC_FILES_ANDROID))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.10       \
    libgstbase-0.10         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0

LOCAL_MODULE:= libgstvideo-0.10

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst-libs/gst/video  \
    $(LOCAL_PATH)/../gst-libs   		\
    $(LOCAL_PATH)/..            		\
    $(LOCAL_PATH)      			 		\
	$(LOCAL_PATH)/gst-libs/gst/video    \
    $(TARGET_OUT_HEADERS)/gstreamer-0.10 \
	$(TARGET_OUT_HEADERS)/glib-2.0 		 \
    $(TARGET_OUT_HEADERS)/glib-2.0/glib  \
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

LOCAL_COPY_HEADERS_TO := $(gst_video_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_video_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
