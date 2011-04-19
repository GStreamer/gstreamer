plugin_LTLIBRARIES = libgstmpegstream.la

libgstmpegstream_la_SOURCES = gstmpegstream.c \
                              gstmpegparse.c \
                              gstmpegdemux.c \
                              gstdvddemux.c \
                              gstmpegpacketize.c \
                              gstmpegclock.c
# gstrfc2250enc.c
libgstmpegstream_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstmpegstream_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_MAJORMINOR@
libgstmpegstream_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegstream_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstmpegparse.h \
                 gstmpegdemux.h \
                 gstdvddemux.h \
                 gstmpegpacketize.h \
                 gstmpegclock.h \
		 gstrfc2250enc.h

EXTRA_DIST = README notes

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstmpegstream -:SHARED libgstmpegstream \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstmpegstream_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstmpegstream_la_CFLAGS) \
	 -:LDFLAGS $(libgstmpegstream_la_LDFLAGS) \
	           $(libgstmpegstream_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
