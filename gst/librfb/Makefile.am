# please keep librfb easily extractable

noinst_LTLIBRARIES = librfb.la
plugin_LTLIBRARIES = libgstrfbsrc.la

libgstrfbsrc_la_SOURCES = gstrfbsrc.c
libgstrfbsrc_la_CFLAGS = $(GST_CFLAGS) -I$(srcdir)/..
libgstrfbsrc_la_LIBADD = $(GST_BASE_LIBS) librfb.la
libgstrfbsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

librfb_la_SOURCES = \
	rfbbuffer.c \
	rfbdecoder.c \
	rfbbytestream.c
librfb_la_CFLAGS = $(GST_CFLAGS) -I$(srcdir)/..
librfb_la_LIBADD = $(GLIB_LIBS)

noinst_HEADERS = \
	rfb.h \
	rfbdecoder.h \
	rfbbuffer.h \
	rfbbytestream.h \
	rfbcontext.h \
	rfbutil.h \
	gstrfbsrc.h
