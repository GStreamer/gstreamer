#noinst_PROGRAMS = basic m types a r plugin w s args mpg123 mcut push qtest
noinst_PROGRAMS = qtest spectrum record wave mp3 teardown buffer mp3parse \
		  mpeg2parse mp1parse mp3play ac3parse ac3play dvdcat fake cobin videotest \
			aviparse vidcapture avi2mpg mp2tomp1

SUBDIRS = xml cothreads bindings

spectrum_CFLAGS = $(shell gnome-config --cflags gnomeui)
spectrum_LDFLAGS = $(shell gnome-config --libs gnomeui)
wave_CFLAGS = $(shell gnome-config --cflags gnomeui)
wave_LDFLAGS = $(shell gnome-config --libs gnomeui)
videotest_CFLAGS = $(shell gnome-config --cflags gnomeui)
videotest_LDFLAGS = $(shell gnome-config --libs gnomeui)
aviparse_CFLAGS = $(shell gnome-config --cflags gnomeui)
aviparse_LDFLAGS = $(shell gnome-config --libs gnomeui)
mp1parse_CFLAGS = $(shell gnome-config --cflags gnomeui)
mp1parse_LDFLAGS = $(shell gnome-config --libs gnomeui)
mpeg2parse_CFLAGS = $(shell gnome-config --cflags gnomeui)
mpeg2parse_LDFLAGS = $(shell gnome-config --libs gnomeui)
mp2tomp1_CFLAGS = $(shell gnome-config --cflags gnomeui)
mp2tomp1_LDFLAGS = $(shell gnome-config --libs gnomeui)

buffer_SOURCES = buffer.c mem.c
teardown_SOURCES = teardown.c mem.c
ac3play_SOURCES = ac3play.c mem.c

noinst_HEADERS = mem.h

#LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_builddir)/gst/libgst.la \
#          $(top_builddir)/plugins/videosink/libvideosink.la -L/usr/X11/lib -lXxf86dga
LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_builddir)/gst/libgst.la


INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) \
           $(shell gnome-config --cflags gnomeui)

EXTRA_DIST = README
