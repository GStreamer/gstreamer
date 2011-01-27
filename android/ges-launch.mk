LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE:= ges-launch-0.10
#LOCAL_ARM_MODE := arm

LOCAL_SRC_FILES := ges-launch.c
LOCAL_SRC_FILES := $(addprefix ../tools/,$(LOCAL_SRC_FILES))

LOCAL_CFLAGS := $(GST_CFLAGS) \
	-I$(GES_TOP) \
	$(shell $(PKG_CONFIG) gstreamer-pbutils --cflags)

LOCAL_SHARED_LIBRARIES := \
	libges-0.10		\
	libgstpbutils-0.10	\
	libgstcontroller-0.10	\
	libgstvideo-0.10	\
	libgstreamer-0.10       \
	libglib-2.0             \
	libgthread-2.0          \
	libgmodule-2.0          \
	libgobject-2.0

LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_TAGS := optional

include $(BUILD_EXECUTABLE)
