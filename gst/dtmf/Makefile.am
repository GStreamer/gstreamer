plugin_LTLIBRARIES = libgstdtmf.la

libgstdtmf_la_SOURCES = gstdtmfsrc.c \
                        gstdtmfdetect.c \
                        gstrtpdtmfsrc.c \
                        gstrtpdtmfdepay.c \
                        tone_detect.c \
                        gstdtmf.c

noinst_HEADERS = gstdtmfsrc.h \
                 gstdtmfdetect.h \
                 gstrtpdtmfsrc.h \
                 gstrtpdtmfdepay.h \
                 gstdtmfcommon.h \
                 tone_detect.h

libgstdtmf_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) \
	-DEXTERN_BUF -DRTP_SUPPORT
libgstdtmf_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstrtp-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstdtmf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdtmf_la_LIBTOOLFLAGS = --tag=disable-static

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstdtmf -:SHARED libgstdtmf \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstdtmf_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstdtmf_la_CFLAGS) \
	 -:LDFLAGS $(libgstdtmf_la_LDFLAGS) \
	           $(libgstdtmf_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@