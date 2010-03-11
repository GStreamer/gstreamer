plugin_LTLIBRARIES = libgstffmpegcolorspace.la

libgstffmpegcolorspace_la_SOURCES = \
	gstffmpegcolorspace.c \
	gstffmpegcodecmap.c \
	dsputil.c \
	mem.c \
	utils.c \
	imgconvert.c

libgstffmpegcolorspace_la_CFLAGS = $(GST_CFLAGS)
libgstffmpegcolorspace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstffmpegcolorspace_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)
libgstffmpegcolorspace_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstffmpegcolorspace.h \
	gstffmpegcodecmap.h \
	imgconvert_template.h \
	dsputil.h \
	avcodec.h
