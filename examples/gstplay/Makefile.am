
noinst_PROGRAMS = player

player_SOURCES = player.c
player_CFLAGS = $(GST_CFLAGS) $(GCONF_CFLAGS)
player_LDFLAGS = \
    $(GST_LIBS) \
    $(top_builddir)/gst-libs/gst/gconf/libgstgconf-@GST_MAJORMINOR@.la \
    $(top_builddir)/gst-libs/gst/play/libgstplay-@GST_MAJORMINOR@.la \
    $(top_builddir)/gst-libs/gst/libgstinterfaces-$(GST_MAJORMINOR).la

