LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

GES_TOP := $(LOCAL_PATH)

GES_BUILT_SOURCES :=            	\
        ges/Android.mk		\
	tools/Android.mk
GES_BUILT_SOURCES := $(patsubst %, $(abspath $(GES_TOP))/%, $(GES_BUILT_SOURCES))

.PHONY: ges-configure ges-configure-real
ges-configure-real:
	cd $(GES_TOP) ; \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS)" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG_LIBDIR=$(CONFIGURE_PKG_CONFIG_LIBDIR) \
	PKG_CONFIG_TOP_BUILD_DIR=/ \
	$(abspath $(GES_TOP))/$(CONFIGURE) --prefix=/system --host=arm-linux-androideabi \
	--disable-gtk-doc --disable-valgrind && \
	for file in $(GES_BUILT_SOURCES); do \
		rm -f $$file && \
		make -C $$(dirname $$file) $$(basename $$file) ; \
	done

ges-configure: ges-configure-real

CONFIGURE_TARGETS += ges-configure

-include $(GES_TOP)/ges/Android.mk
-include $(GES_TOP)/tools/Android.mk
