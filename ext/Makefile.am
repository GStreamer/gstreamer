SUBDIRS = libvpx

EXTRA_DIST =

codecparsers_srcdir = \
	$(top_srcdir)/ext/codecparsers/gst-libs/gst/codecparsers

codecparsers_source_c = \
	dboolhuff.c		\
	gsth264parser.c		\
	gstjpegparser.c		\
	gstmpeg4parser.c	\
	gstmpegvideoparser.c	\
	gstvc1parser.c		\
	gstvp8parser.c		\
	gstvp8rangedecoder.c	\
	nalutils.c		\
	parserutils.c		\
	vp8utils.c		\
	$(NULL)

EXTRA_DIST += $(codecparsers_source_c:%.c=$(codecparsers_srcdir)/%.c)

codecparsers_source_h = \
	dboolhuff.h		\
	gsth264parser.h		\
	gstjpegparser.h		\
	gstmpeg4parser.h	\
	gstmpegvideoparser.h	\
	gstvc1parser.h		\
	gstvp8parser.h		\
	gstvp8rangedecoder.h	\
	nalutils.h		\
	parserutils.h		\
	vp8utils.h		\
	$(NULL)

EXTRA_DIST += $(codecparsers_source_h:%.h=$(codecparsers_srcdir)/%.h)

videoparsers_srcdir = \
	$(top_srcdir)/ext/codecparsers/gst/videoparsers

videoparsers_source_c = \
	gsth264parse.c		\
	$(NULL)

EXTRA_DIST += $(videoparsers_source_c:%.c=$(videoparsers_srcdir)/%.c)

videoparsers_source_h = \
	gsth264parse.h		\
	$(NULL)

EXTRA_DIST += $(videoparsers_source_h:%.h=$(videoparsers_srcdir)/%.h)

videoutils_srcdir = \
	$(top_srcdir)/ext/videoutils/gst-libs/gst/video

videoutils_source_c = \
	gstvideodecoder.c	\
	gstvideoencoder.c	\
	gstvideoutils.c		\
	video.c			\
	$(NULL)

EXTRA_DIST += $(videoutils_source_c:%.c=$(videoutils_srcdir)/%.c)

videoutils_source_h = \
	gstvideodecoder.h	\
	gstvideoencoder.h	\
	gstvideoutils.h		\
	video.h			\
	$(NULL)

EXTRA_DIST += $(videoutils_source_h:%.h=$(videoutils_srcdir)/%.h)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
