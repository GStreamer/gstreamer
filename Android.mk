# This file is the top android makefile for all sub-modules.

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

gstreamer_TOP := $(LOCAL_PATH)

GST_BUILT_SOURCES := 		\
	gst/gstenumtypes.h 		\
	gst/gstenumtypes.c 		\
	gst/gstmarshal.h 		\
	gst/gstmarshal.c 		\
	gst/parse/grammar.tab.h	\
	gst/parse/grammar.tab.c	\
	gst/parse/grammar.output	\
	gst/parse/lex._gst_parse_yy.c \
	pkgconfig/gstreamer-0.10.pc       \
	pkgconfig/gstreamer-base-0.10.pc      \
	pkgconfig/gstreamer-controller-0.10.pc    \
	pkgconfig/gstreamer-dataprotocol-0.10.pc    \
	pkgconfig/gstreamer-net-0.10.pc	\
	pkgconfig/gstreamer-0.10-uninstalled.pc       \
	pkgconfig/gstreamer-base-0.10-uninstalled.pc      \
	pkgconfig/gstreamer-controller-0.10-uninstalled.pc    \
	pkgconfig/gstreamer-dataprotocol-0.10-uninstalled.pc    \
	pkgconfig/gstreamer-net-0.10-uninstalled.pc	\
	gst/Android.mk \
	gst/parse/Android.mk \
	libs/Android.mk \
	libs/gst/Android.mk \
	libs/gst/base/Android.mk \
	libs/gst/controller/Android.mk \
	libs/gst/dataprotocol/Android.mk \
	libs/gst/net/Android.mk \
	libs/gst/helpers/Android.mk \
	plugins/Android.mk \
	plugins/elements/Android.mk \
	plugins/indexers/Android.mk \
	tools/Android.mk

GST_BUILT_SOURCES := $(patsubst %, $(abspath $(gstreamer_TOP))/%, $(GST_BUILT_SOURCES))

.PHONY: gst-configure gst-configure-real
gst-configure-real:
	echo $(GST_BUILT_SOURCES)
	cd $(gstreamer_TOP) ; \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS)" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG_LIBDIR=$(CONFIGURE_PKG_CONFIG_LIBDIR) \
	PKG_CONFIG_TOP_BUILD_DIR=/ \
	$(abspath $(gstreamer_TOP))/$(CONFIGURE) --host=arm-linux-androideabi \
	--prefix=/system --disable-nls \
	--disable-valgrind --disable-gtk-doc && \
	for file in $(GST_BUILT_SOURCES); do \
		rm -f $$file && \
		make -C $$(dirname $$file) $$(basename $$file) ; \
	done

gst-configure: gst-configure-real

CONFIGURE_TARGETS += gst-configure

-include $(gstreamer_TOP)/gst/Android.mk
-include $(gstreamer_TOP)/libs/Android.mk
-include $(gstreamer_TOP)/plugins/Android.mk
-include $(gstreamer_TOP)/tools/Android.mk
