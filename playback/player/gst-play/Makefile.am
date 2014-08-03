bin_PROGRAMS = gst-play

gst_play_SOURCES = gst-play.c gst-play-kb.c gst-play-kb.h

LDADD = $(top_builddir)/lib/gst/player/.libs/libgstplayer-@GST_PLAYER_API_VERSION@.la \
	$(GSTREAMER_LIBS) $(GLIB_LIBS) $(LIBM)

AM_CFLAGS = -I$(top_srcdir)/lib -I$(top_builddir)/lib $(GSTREAMER_CFLAGS) $(GLIB_CFLAGS) $(WARNING_CFLAGS)

noinst_HEADERS = gst-play-kb.h
