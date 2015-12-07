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
	gsth265parser.c         \
	gstvp9parser.c          \
	vp9utils.c              \
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
	gsth265parser.h         \
	gstvp9parser.h          \
	vp9utils.h              \
	$(NULL)

EXTRA_DIST += $(codecparsers_source_h:%.h=$(codecparsers_srcdir)/%.h)

videoparsers_srcdir = \
	$(top_srcdir)/ext/codecparsers/gst/videoparsers

videoparsers_source_c = \
	gsth264parse.c		\
	gsth265parse.c		\
	$(NULL)

EXTRA_DIST += $(videoparsers_source_c:%.c=$(videoparsers_srcdir)/%.c)

videoparsers_source_h = \
	gsth264parse.h		\
	gsth265parse.h		\
	$(NULL)

EXTRA_DIST += $(videoparsers_source_h:%.h=$(videoparsers_srcdir)/%.h)

-include $(top_srcdir)/git.mk
