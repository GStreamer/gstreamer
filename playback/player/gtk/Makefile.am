bin_PROGRAMS = gtk-play

gtk_play_SOURCES = gtk-play.c

LDADD = $(top_builddir)/lib/gst/player/.libs/libgstplayer-@GST_PLAYER_API_VERSION@.la \
	$(GSTREAMER_LIBS) $(GTK_LIBS) $(GLIB_LIBS) $(LIBM)

AM_CFLAGS = -I$(top_srcdir)/lib -I$(top_builddir)/lib $(GSTREAMER_CFLAGS) $(GTK_CFLAGS) $(GLIB_CFLAGS)

noinst_HEADERS =
