EXTRA_DIST =

codecparsers_srcdir = \
	$(top_srcdir)/ext/codecparsers/gst-libs/gst/codecparsers

codecparsers_source_c = \
	gsth264parser.c		\
	gstjpegparser.c		\
	gstmpeg4parser.c	\
	gstmpegvideoparser.c	\
	gstvc1parser.c		\
	parserutils.c		\
	$(NULL)

EXTRA_DIST += $(codecparsers_source_c:%.c=$(codecparsers_srcdir)/%.c)

codecparsers_source_h = \
	gsth264parser.h		\
	gstjpegparser.h		\
	gstmpeg4parser.h	\
	gstmpegvideoparser.h	\
	gstvc1parser.h		\
	parserutils.h		\
	$(NULL)

EXTRA_DIST += $(codecparsers_source_h:%.h=$(codecparsers_srcdir)/%.h)

videoutils_srcdir = \
	$(top_srcdir)/ext/videoutils/gst-libs/gst/video

videoutils_source_c = \
	gstvideodecoder.c	\
	gstvideoutils.c		\
	video.c			\
	$(NULL)

EXTRA_DIST += $(videoutils_source_c:%.c=$(videoutils_srcdir)/%.c)

videoutils_source_h = \
	gstvideodecoder.h	\
	gstvideoutils.h		\
	video.h			\
	$(NULL)

EXTRA_DIST += $(videoutils_source_h:%.h=$(videoutils_srcdir)/%.h)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
