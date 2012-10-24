plugin_LTLIBRARIES = libgstsiren.la

libgstsiren_la_SOURCES = gstsiren.c \
	gstsirendec.c gstsirendec.h \
	gstsirenenc.c gstsirenenc.h \
	common.c dct4.c decoder.c encoder.c huffman.c \
	huffman.h rmlt.h common.h dct4.h decoder.h \
	encoder.h huffman_consts.h rmlt.c siren7.h


libgstsiren_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstsiren_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstsiren_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsiren_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstsiren -:SHARED libgstsiren \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstsiren_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstsiren_la_CFLAGS) \
	 -:LDFLAGS $(libgstsiren_la_LDFLAGS) \
	           $(libgstsiren_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@