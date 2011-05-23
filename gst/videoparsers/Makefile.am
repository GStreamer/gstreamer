plugin_LTLIBRARIES = libgstvideoparsersbad.la

libgstvideoparsersbad_la_SOURCES = plugin.c \
	h263parse.c gsth263parse.c \
	gsth264parse.c h264parse.c \
	gstdiracparse.c dirac_parse.c \
	gstmpegvideoparse.c mpegvideoparse.c
libgstvideoparsersbad_la_CFLAGS = \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstvideoparsersbad_la_LIBADD = \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstvideoparsersbad_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideoparsersbad_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gsth263parse.h h263parse.h \
	gsth264parse.h h264parse.h \
	gstdiracparse.h dirac_parse.h \
	gstmpegvideoparse.h mpegvideoparse.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstvideoparsersbad -:SHARED libgstvideoparsersbad \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstvideoparsersbad_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstvideoparsersbad_la_CFLAGS) \
	 -:LDFLAGS $(libgstvideoparsersbad_la_LDFLAGS) \
	           $(libgstvideoparsersbad_la_LIBADD) \
	           -ldl \
	 -:LIBFILTER_STATIC gstbaseparse-@GST_MAJORMINOR@ \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
