plugin_LTLIBRARIES = libgstffmpegcolorspace.la

libgstffmpegcolorspace_la_SOURCES = \
	gstffmpeg.c \
	gstffmpegcolorspace.c \
	gstffmpegcodecmap.c \
	dsputil.c \
	mem.c \
	utils.c \
	imgconvert.c

libgstffmpegcolorspace_la_CFLAGS = $(GST_CFLAGS)
libgstffmpegcolorspace_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstffmpegcodecmap.h \
	imgconvert_template.h \
	mmx.h \
	avcodec.h
