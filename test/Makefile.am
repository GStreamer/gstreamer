# FIXME FIXME

noinst_PROGRAMS = qtest spectrum record wave mp3 teardown buffer mp3parse \
		  mpeg2parse mp1parse mp3play ac3parse ac3play dvdcat fake cobin videotest \
		  aviparse vidcapture avi2mpg mp2tomp1 mp1tomp1 pipetest \
		  vidcapture2 mp2toavi mp3tovorbis mpeg2parse2 xmmstest videotest2 \
		  mp3mad video2mp1 dvshow dv2mp1

SUBDIRS = xml bindings

buffer_SOURCES = buffer.c mem.c
teardown_SOURCES = teardown.c mem.c
ac3play_SOURCES = ac3play.c mem.c

noinst_HEADERS = mem.h

if HAVE_LIBXV
xvlibs=-lXv
else
xvlibs=
endif

LDADD = ${xvlibs} -lXxf86vm $(GNOME_LIBS) $(GST_LIBS)

#LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_builddir)/gst/libgst.la \
#          $(top_builddir)/plugins/videosink/libvideosink.la -L/usr/X11/lib -lXxf86dga
#LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_builddir)/gst/libgst.la

LIBS += $(GNOME_LIBS) $(GST_LIBS)
CFLAGS += $(GNOME_CFLAGS) $(GST_CFLAGS)

EXTRA_DIST = README
