plugin_LTLIBRARIES = libgstmve.la

libgstmve_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmve_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstmve_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmve_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

libgstmve_la_SOURCES = \
	gstmve.c \
	gstmvemux.c \
	gstmvedemux.c \
	mveaudiodec.c \
	mvevideodec8.c \
	mvevideodec16.c \
	mveaudioenc.c \
	mvevideoenc8.c \
	mvevideoenc16.c

noinst_HEADERS = gstmvedemux.h gstmvemux.h mve.h

EXTRA_DIST = TODO

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmve -:SHARED libgstmve \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmve_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmve_la_CFLAGS) \
	 -:LDFLAGS $(libgstmve_la_LDFLAGS) \
	           $(libgstmve_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
