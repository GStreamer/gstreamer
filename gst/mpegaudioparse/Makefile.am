#FIXME clean me up a bit

filterdir = $(libdir)/gst
filter_LTLIBRARIES = libgstmpegaudioparse.la libgstmp3types.la

libgstmpegaudioparse_la_SOURCES = gstmpegaudioparse.c
# FIXME is this useful?
libgstmpegaudioparse_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) -ffast-math -finline-functions $(GST_CFLAGS)

libgstmp3types_la_SOURCES = gstmp3types.c
libgstmp3types_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) -ffast-math -finline-functions $(GST_CFLAGS)

noinst_HEADERS = gstmpegaudioparse.h
EXTRA_DIST = README

# FIXME is this needed?
