
noinst_PROGRAMS = player

AM_CFLAGS = $(GST_CFLAGS)

player_SOURCES = player.c
player_LDFLAGS = \
    $(GST_LIBS) \
    $(top_builddir)/gst-libs/gst/play/libgstplay-@GST_MAJORMINOR@.la

