#noinst_PROGRAMS = basic m types a r plugin w s args mpg123 mcut push qtest
noinst_PROGRAMS = qtest spectrum record wave mp3 teardown buffer mp3parse \
		  mpeg2parse mp1parse mp3play ac3parse ac3play dvdcat fake cobin videotest \
		  aviparse vidcapture avi2mpg mp2tomp1 mp1tomp1 pipetest \
		  vidcapture2 mp2toavi

SUBDIRS = xml cothreads bindings

CFLAGS = -Wall $(shell gnome-config --cflags gnomeui)
LDFLAGS = $(shell gnome-config --libs gnomeui)

buffer_SOURCES = buffer.c mem.c
teardown_SOURCES = teardown.c mem.c
ac3play_SOURCES = ac3play.c mem.c

noinst_HEADERS = mem.h

LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_builddir)/gst/libgst.la \
          $(top_builddir)/plugins/videosink/gdkxvimage.lo -lXv -lXxf86vm

#LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_builddir)/gst/libgst.la \
#          $(top_builddir)/plugins/videosink/libvideosink.la -L/usr/X11/lib -lXxf86dga
#LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_builddir)/gst/libgst.la


INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) \
           $(shell gnome-config --cflags gnomeui)

EXTRA_DIST = README
