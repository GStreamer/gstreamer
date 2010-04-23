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
