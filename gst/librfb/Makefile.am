
# please keep librfb easily extractable

noinst_LTLIBRARIES = librfb.la
plugin_LTLIBRARIES = libgstrfbsrc.la

libgstrfbsrc_la_SOURCES =  gstrfbsrc.c
libgstrfbsrc_la_CFLAGS =  $(GST_CFLAGS) -I/usr/local/include/librfb-0.1
libgstrfbsrc_la_LIBADD = librfb.la
libgstrfbsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)


noinst_HEADERS = \
	rfb.h \
	rfbdecoder.h \
	rfbbuffer.h \
	rfbbytestream.h

librfb_la_SOURCES = \
	rfb.h \
	rfbdecoder.c \
	rfbdecoder.h \
	rfbbuffer.c \
	rfbbuffer.h \
	rfbbytestream.c \
	rfbbytestream.h

librfb_la_CFLAGS = $(RFB_CFLAGS)
librfb_la_LIBADD = $(GLIB_LIBS)

