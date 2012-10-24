plugin_LTLIBRARIES = libgstmxf.la

libgstmxf_la_SOURCES = \
	mxf.c \
	mxful.c \
	mxftypes.c \
	mxfmetadata.c \
	mxfessence.c \
	mxfquark.c \
	mxfmux.c \
	mxfdemux.c \
	mxfaes-bwf.c \
	mxfmpeg.c \
	mxfdv-dif.c \
	mxfalaw.c \
	mxfjpeg2000.c \
	mxfd10.c \
	mxfup.c \
	mxfvc3.c \
	mxfdms1.c

libgstmxf_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstmxf_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_API_VERSION@ \
	-lgstaudio-@GST_API_VERSION@
libgstmxf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmxf_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	mxful.h \
	mxftypes.h \
	mxfmetadata.h \
	mxfquark.h \
	mxfessence.h \
	mxfdemux.h \
	mxfmux.h \
	mxfaes-bwf.h \
	mxfmpeg.h \
	mxfdv-dif.h \
	mxfalaw.h \
	mxfjpeg2000.h \
	mxfd10.h \
	mxfup.h \
	mxfvc3.h \
	mxfdms1.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmxf -:SHARED libgstmxf \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmxf_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmxf_la_CFLAGS) \
	 -:LDFLAGS $(libgstmxf_la_LDFLAGS) \
	           $(libgstmxf_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
