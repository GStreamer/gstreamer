plugin_LTLIBRARIES = libgstffmpegcolorspace.la

libgstffmpegcolorspace_la_SOURCES = \
	gstffmpegcolorspace.c \
	gstffmpegcodecmap.c \
	dsputil.c \
	mem.c \
	utils.c \
	imgconvert.c

libgstffmpegcolorspace_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstffmpegcolorspace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstffmpegcolorspace_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_MAJORMINOR).la \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstffmpegcolorspace_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstffmpegcolorspace.h \
	gstffmpegcodecmap.h \
	imgconvert_template.h \
	dsputil.h \
	avcodec.h

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstffmpegcolorspace -:SHARED libgstffmpegcolorspace \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstffmpegcolorspace_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstffmpegcolorspace_la_CFLAGS) \
	 -:LDFLAGS $(libgstffmpegcolorspace_la_LDFLAGS) \
	           $(libgstffmpegcolorspace_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
