plugin_LTLIBRARIES = libgstmpegpsmux.la

libgstmpegpsmux_la_SOURCES = \
	mpegpsmux.c \
	psmux.c \
	psmuxstream.c \
	mpegpsmux_aac.c \
	mpegpsmux_h264.c

libgstmpegpsmux_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegpsmux_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegpsmux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	mpegpsmux.h \
	psmux.h \
	psmuxstream.h \
	psmuxcommon.h \
	mpegpsmux_aac.h \
	mpegpsmux_h264.h \
	bits.h \
	crc.h
