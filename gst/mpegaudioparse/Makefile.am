#FIXME clean me up a bit

plugindir = $(libdir)/gst
plugin_LTLIBRARIES = libgstmpegaudioparse.la libgstmp3types.la

libgstmpegaudioparse_la_SOURCES = gstmpegaudioparse.c
# FIXME is this useful?
libgstmpegaudioparse_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) -ffast-math -finline-functions $(GST_CFLAGS)
libgstmpegaudioparse_la_LIBADD = $(GST_LIBS)
libgstmpegaudioparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstmp3types_la_SOURCES = gstmp3types.c
libgstmp3types_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) -ffast-math -finline-functions $(GST_CFLAGS)
libgstmp3types_la_LIBADD = $(GST_LIBS)
libgstmp3types_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmpegaudioparse.h
EXTRA_DIST = README

# FIXME is this needed?
