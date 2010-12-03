LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

rtp_LOCAL_SRC_FILES:= \
	gst/rtp/fnv1hash.c \
	gst/rtp/gstrtp.c \
	gst/rtp/gstrtpchannels.c \
	gst/rtp/gstrtpdepay.c \
	gst/rtp/gstrtpac3depay.c \
	gst/rtp/gstrtpbvdepay.c \
	gst/rtp/gstrtpbvpay.c \
	gst/rtp/gstrtpceltdepay.c \
	gst/rtp/gstrtpceltpay.c \
	gst/rtp/gstrtpdvdepay.c \
	gst/rtp/gstrtpdvpay.c \
	gst/rtp/gstrtpilbcdepay.c \
	gst/rtp/gstrtpilbcpay.c \
	gst/rtp/gstrtpmpadepay.c \
	gst/rtp/gstrtpmpapay.c \
	gst/rtp/gstrtpmpvdepay.c \
	gst/rtp/gstrtpmpvpay.c \
	gst/rtp/gstrtppcmadepay.c \
	gst/rtp/gstrtppcmudepay.c \
	gst/rtp/gstrtppcmupay.c \
	gst/rtp/gstrtppcmapay.c \
	gst/rtp/gstrtpg723depay.c \
	gst/rtp/gstrtpg723pay.c \
	gst/rtp/gstrtpg726pay.c \
	gst/rtp/gstrtpg726depay.c \
	gst/rtp/gstrtpg729pay.c \
	gst/rtp/gstrtpg729depay.c \
	gst/rtp/gstrtpgsmdepay.c \
	gst/rtp/gstrtpgsmpay.c \
	gst/rtp/gstrtpamrdepay.c \
	gst/rtp/gstrtpamrpay.c \
	gst/rtp/gstrtph263pdepay.c \
	gst/rtp/gstrtph263ppay.c \
	gst/rtp/gstrtph263depay.c \
	gst/rtp/gstrtph263pay.c \
	gst/rtp/gstrtph264depay.c \
	gst/rtp/gstrtph264pay.c \
	gst/rtp/gstrtpj2kdepay.c \
	gst/rtp/gstrtpj2kpay.c \
	gst/rtp/gstrtpjpegdepay.c \
	gst/rtp/gstrtpjpegpay.c \
	gst/rtp/gstrtpL16depay.c \
	gst/rtp/gstrtpL16pay.c \
	gst/rtp/gstasteriskh263.c \
	gst/rtp/gstrtpmp1sdepay.c \
	gst/rtp/gstrtpmp2tdepay.c \
	gst/rtp/gstrtpmp2tpay.c \
	gst/rtp/gstrtpmp4vdepay.c \
	gst/rtp/gstrtpmp4vpay.c \
	gst/rtp/gstrtpmp4gdepay.c \
	gst/rtp/gstrtpmp4gpay.c \
	gst/rtp/gstrtpmp4adepay.c \
	gst/rtp/gstrtpmp4apay.c \
	gst/rtp/gstrtpqdmdepay.c \
	gst/rtp/gstrtpsirenpay.c \
	gst/rtp/gstrtpsirendepay.c \
	gst/rtp/gstrtpspeexdepay.c \
	gst/rtp/gstrtpspeexpay.c \
	gst/rtp/gstrtpsv3vdepay.c \
	gst/rtp/gstrtptheoradepay.c \
	gst/rtp/gstrtptheorapay.c \
	gst/rtp/gstrtpvorbisdepay.c \
	gst/rtp/gstrtpvorbispay.c  \
	gst/rtp/gstrtpvrawdepay.c  \
	gst/rtp/gstrtpvrawpay.c 

LOCAL_SRC_FILES:= $(addprefix ../,$(rtp_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
	libgstreamer-0.10 		\
	libgstbase-0.10 		\
	libglib-2.0    			\
	libgthread-2.0 			\
	libgmodule-2.0 			\
	libgobject-2.0 			\
	libgsttag-0.10 			\
	libgstrtp-0.10 			\
	libgstaudio-0.10

LOCAL_MODULE:= libgstrtp

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/../gst/rtp  			\
	$(LOCAL_PATH)/..         			\
	$(LOCAL_PATH)/../gst-libs 			\
	$(LOCAL_PATH)  						\
	$(TARGET_OUT_HEADERS)/gstreamer-0.10 \
	$(TARGET_OUT_HEADERS)/glib-2.0 		\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib \
	external/libxml2/include

ifeq ($(STECONF_ANDROID_VERSION),"FROYO")
LOCAL_SHARED_LIBRARIES += libicuuc 
LOCAL_C_INCLUDES += external/icu4c/common
endif

LOCAL_CFLAGS := -DHAVE_CONFIG_H
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

#It's a gstreamer plugins, and it must be installed on ..../lib/gstreamer-0.10
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.10

include $(BUILD_SHARED_LIBRARY)
