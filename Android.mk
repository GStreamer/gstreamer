LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

GST_PLUGINS_UGLY_TOP := $(LOCAL_PATH)

GST_PLUGINS_UGLY_BUILT_SOURCES = \
	ext/mad/Android.mk \
	gst/synaesthesia/Android.mk \
	gst/iec958/Android.mk \
	gst/mpegaudioparse/Android.mk \
	gst/mpegstream/Android.mk \
	gst/realmedia/Android.mk \
	gst/dvdsub/Android.mk \
	gst/dvdlpcmdec/Android.mk \
	gst/asfdemux/Android.mk \
	ext/x264/Android.mk

GST_PLUGINS_UGLY_BUILT_SOURCES := $(patsubst %, $(abspath $(GST_PLUGINS_UGLY_TOP))/%, $(GST_PLUGINS_UGLY_BUILT_SOURCES))

.PHONY: gst-plugins-ugly-configure gst-plugins-ugly-configure-real

gst-plugins-ugly-configure-real:
	cd $(GST_PLUGINS_UGLY_TOP) ; \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS)" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG_LIBDIR="$(CONFIGURE_PKG_CONFIG_LIBDIR)" \
	PKG_CONFIG_TOP_BUILD_DIR=/ \
	$(abspath $(GST_PLUGINS_UGLY_TOP))/$(CONFIGURE) --host=arm-linux-androideabi \
	--prefix=/system --disable-orc --disable-valgrind --disable-gtk-doc && \
	for file in $(GST_PLUGINS_UGLY_BUILT_SOURCES); do \
		rm -f $$file && \
		make -C $$(dirname $$file) $$(basename $$file) ; \
	done

gst-plugins-ugly-configure: gst-plugins-ugly-configure-real

CONFIGURE_TARGETS += gst-plugins-ugly-configure

-include $(GST_PLUGINS_UGLY_TOP)/ext/mad/Android.mk
-include $(GST_PLUGINS_UGLY_TOP)/ext/x264/Android.mk
-include $(GST_PLUGINS_UGLY_TOP)/gst/synaesthesia/Android.mk
-include $(GST_PLUGINS_UGLY_TOP)/gst/iec958/Android.mk
-include $(GST_PLUGINS_UGLY_TOP)/gst/mpegaudioparse/Android.mk
-include $(GST_PLUGINS_UGLY_TOP)/gst/mpegstream/Android.mk
-include $(GST_PLUGINS_UGLY_TOP)/gst/realmedia/Android.mk
-include $(GST_PLUGINS_UGLY_TOP)/gst/dvdsub/Android.mk
-include $(GST_PLUGINS_UGLY_TOP)/gst/dvdlpcmdec/Android.mk
-include $(GST_PLUGINS_UGLY_TOP)/gst/asfdemux/Android.mk
