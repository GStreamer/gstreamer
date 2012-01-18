LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

GST_OMX_TOP := $(LOCAL_PATH)

GST_OMX_BUILT_SOURCES := omx/Android.mk

GST_OMX_BUILT_SOURCES := $(patsubst %, $(abspath $(GST_OMX_TOP))/%, $(GST_OMX_BUILT_SOURCES))

.PHONY: gst-omx-configure
gst-omx-configure:
	cd $(GST_OMX_TOP) ; \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS)" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG_LIBDIR="$(CONFIGURE_PKG_CONFIG_LIBDIR)" \
	PKG_CONFIG_TOP_BUILD_DIR=/ \
	$(abspath $(GST_OMX_TOP))/$(CONFIGURE) --host=arm-linux-androideabi \
	--prefix=/system --disable-orc --disable-valgrind --disable-gtk-doc && \
	for file in $(GST_OMX_BUILT_SOURCES); do \
		rm -f $$file && \
		make -C $$(dirname $$file) $$(basename $$file) ; \
	done

CONFIGURE_TARGETS += gst-omx-configure

-include $(GST_OMX_TOP)/omx/Android.mk
