#FIXME clean me up a bit

plugin_LTLIBRARIES = libgstmpegaudioparse.la libgstmp3types.la

libgstmpegaudioparse_la_SOURCES = gstmpegaudioparse.c
# FIXME is this useful?
libgstmpegaudioparse_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) $(GST_OPT_CFLAGS) $(GST_CFLAGS)
libgstmpegaudioparse_la_LIBADD =
libgstmpegaudioparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstmp3types_la_SOURCES = gstmp3types.c
libgstmp3types_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) $(GST_OPT_CFLAGS) $(GST_CFLAGS)
libgstmp3types_la_LIBADD =
libgstmp3types_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmpegaudioparse.h
EXTRA_DIST = README

# FIXME is this needed?
