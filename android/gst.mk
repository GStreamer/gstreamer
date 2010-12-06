LOCAL_PATH:= $(call my-dir)

#------------------------------------
#  include section 
#-----------------------------------
gst_COPY_HEADERS_TO := gstreamer-0.11/gst
gst_COPY_HEADERS_BASE := \
		glib-compat.h       \
		gst.h 				\
		gstbin.h 			\
		gstbuffer.h 		\
		gstbufferlist.h  	\
		gstbus.h 			\
		gstcaps.h 			\
		gstchildproxy.h 	\
		gstclock.h 			\
		gstcompat.h 		\
		gstdebugutils.h 	\
		gstelement.h 		\
		gstelementfactory.h \
		gsterror.h 			\
		gstevent.h 			\
		gstfilter.h 		\
		gstformat.h 		\
		gstghostpad.h 		\
		gstindex.h 			\
		gstindexfactory.h 	\
		gstinfo.h 			\
		gstinterface.h 		\
		gstiterator.h 		\
		gstmacros.h 		\
		gstmessage.h 		\
		gstminiobject.h 	\
		gstobject.h 		\
		gstpad.h 			\
		gstpadtemplate.h 	\
		gstparamspecs.h 	\
		gstparse.h 			\
		gstpipeline.h 		\
		gstplugin.h 		\
		gstpluginfeature.h 	\
		gstpluginloader.h	\
		gstpoll.h 			\
		gstpreset.h 		\
		gstquery.h 			\
		gstregistry.h 		\
		gstregistrychunks.h \
		gstsegment.h 		\
		gststructure.h 		\
		gstsystemclock.h 	\
		gsttaglist.h 		\
		gsttagsetter.h 		\
		gsttask.h 			\
		gsttaskpool.h 		\
		gsttrace.h 			\
		gsttypefind.h 		\
		gsttypefindfactory.h \
		gsturi.h 			\
		gstutils.h 			\
		gstvalue.h 			\
		gstxml.h 		 	

gst_COPY_HEADERS_ANDROID := \
		gstconfig.h 	\
		gstversion.h 	\
		gstenumtypes.h  \
		gstmarshal.h 	

gst_COPY_HEADERS := $(addprefix ../gst/,$(gst_COPY_HEADERS_BASE)) \
					$(addprefix ../android/gst/,$(gst_COPY_HEADERS_ANDROID))

#------------------------------------
include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

GST_MAJORMINOR:= 0.11

gst_LOCAL_SRC_FILES_BASE:= \
	gst.c 				\
	gstobject.c 		\
	gstbin.c 			\
	gstbuffer.c 		\
	gstbufferlist.c     \
	gstbus.c 			\
	gstcaps.c 			\
	gstchildproxy.c 	\
	gstclock.c 			\
	gstdebugutils.c 	\
	gstelement.c 		\
	gstelementfactory.c \
	gsterror.c 			\
	gstevent.c 			\
	gstfilter.c 		\
	gstformat.c 		\
	gstghostpad.c 		\
	gstindex.c 			\
	gstindexfactory.c 	\
	gstinfo.c 			\
	gstinterface.c 		\
	gstiterator.c 		\
	gstmessage.c 		\
	gstminiobject.c 	\
	gstpad.c 			\
	gstpadtemplate.c 	\
	gstparamspecs.c 	\
	gstpipeline.c 		\
	gstplugin.c 		\
	gstpluginfeature.c 	\
	gstpluginloader.c	\
	gstpoll.c 			\
	gstpreset.c 		\
	gstquark.c 			\
	gstquery.c 			\
	gstregistry.c 		\
	gstregistrychunks.c	\
	gstsegment.c 		\
	gststructure.c 		\
	gstsystemclock.c 	\
	gsttaglist.c 		\
	gsttagsetter.c 		\
	gsttask.c 			\
	gsttaskpool.c       \
	gsttrace.c 			\
	gsttypefind.c 		\
	gsttypefindfactory.c \
	gsturi.c 			\
	gstutils.c 			\
	gstvalue.c 			\
	gstparse.c 			\
	gstregistrybinary.c \
	gstxml.c 			


gst_LOCAL_SRC_FILES_ANDROID:= \
	gstenumtypes.c 		\
	gstmarshal.c 		\
	parse/grammar.tab.c \
	parse/lex._gst_parse_yy.c

LOCAL_SRC_FILES:= $(addprefix ../gst/,$(gst_LOCAL_SRC_FILES_BASE)) \
				  $(addprefix ../android/gst/,$(gst_LOCAL_SRC_FILES_ANDROID))	
         
LOCAL_STATIC_LIBRARIES := libxml2  
           
LOCAL_SHARED_LIBRARIES := \
    libglib-2.0           \
    libgthread-2.0        \
    libgmodule-2.0        \
    libgobject-2.0 

LOCAL_MODULE:= libgstreamer-$(GST_MAJORMINOR)

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)  						\
	$(LOCAL_PATH)/gst 					\
	$(LOCAL_PATH)/gst/parse 			\
	$(LOCAL_PATH)/../gst              	\
    $(LOCAL_PATH)/..       				\
    $(LOCAL_PATH)/../gst/parse        	\
	$(TARGET_OUT_HEADERS)/glib-2.0 		\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib \
	external/libxml2/include

ifeq ($(STECONF_ANDROID_VERSION),"FROYO")
LOCAL_SHARED_LIBRARIES += libicuuc 
LOCAL_C_INCLUDES += external/icu4c/common
endif

LOCAL_CFLAGS := \
    -D_GNU_SOURCE                                \
    -DG_LOG_DOMAIN=g_log_domain_gstreamer        \
    -DGST_MAJORMINOR=\""$(GST_MAJORMINOR)"\"     \
    -DGST_DISABLE_DEPRECATED                     \
    -DHAVE_CONFIG_H   

#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false 

LOCAL_COPY_HEADERS_TO := $(gst_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
