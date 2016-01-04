bin_PROGRAMS = gst-play

gst_play_SOURCES = gst-play.c gst-play-kb.c gst-play-kb.h

LDADD = $(GSTREAMER_LIBS) $(GLIB_LIBS) $(LIBM)

AM_CFLAGS = $(GSTREAMER_CFLAGS) $(GLIB_CFLAGS) $(WARNING_CFLAGS)

noinst_HEADERS = gst-play-kb.h
