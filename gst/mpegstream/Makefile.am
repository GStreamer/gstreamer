filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstmpegstream.la

libgstmpegstream_la_SOURCES = gstmpegstream.c gstmpegparse.c gstmpegdemux.c gstmpegpacketize.c
libgstmpegstream_la_CFLAGS = $(GST_CFLAGS) -O3 $(FOMIT_FRAME_POINTER) -ffast-math 

noinst_HEADERS = gstmpegparse.h gstmpegdemux.h gstmpegpacketize.h


EXTRA_DIST = README notes
