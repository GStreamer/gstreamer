plugin_LTLIBRARIES = libgstmpegtsdemux.la

libgstmpegtsdemux_la_SOURCES = \
	gsttsdemux.c \
	gstmpegdesc.c \
	mpegtsbase.c	\
	mpegtspacketizer.c \
	mpegtsparse.c \
	payload_parsers.c \
	tsdemux.c	\
	pesparse.c

libgstmpegtsdemux_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegtsdemux_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegtsdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegtsdemux_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstmpegdefs.h   \
	gstmpegdesc.h   \
	mpegtsbase.h	\
	mpegtspacketizer.h \
	mpegtsparse.h \
	payload_parsers.h \
	tsdemux.h	\
	pesparse.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmpegtsdemux -:SHARED libgstmpegtsdemux \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmpegtsdemux_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmpegtsdemux_la_CFLAGS) \
	 -:LDFLAGS $(libgstmpegtsdemux_la_LDFLAGS) \
	           $(libgstmpegtsdemux_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
