# FIXME FIXME

if HAVE_GNOME
GNOME_PROGS = spectrum wave mp1parse videotest aviparse \
		videotest2 video2mp1 dvshow dv2mp1 \
		mpeg2parse2 mpeg2parse3 \
		mp2tomp1v2 main sinesliders testspeed dvdplay testpod
sinesliders_LDADD = ../libs/control/libgstcontrol.la
else
GNOME_PROGS =
endif

noinst_PROGRAMS = qtest $(GNOME_PROGS) record mp3 teardown buffer mp3parse \
		  mp3play ac3parse ac3play dvdcat fake cobin \
		  vidcapture avi2mpg mp2tomp1 mp1tomp1 pipetest \
		  vidcapture2 mp2toavi mp3tovorbis xmmstest \
		  mp3mad lat 

SUBDIRS = xml bindings events memchunk

buffer_SOURCES = buffer.c mem.c
teardown_SOURCES = teardown.c mem.c
ac3play_SOURCES = ac3play.c mem.c

noinst_HEADERS = mem.h

LIBS = $(GNOME_LIBS) $(GST_LIBS)
CFLAGS = $(GNOME_CFLAGS) $(GST_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)

EXTRA_DIST = README
