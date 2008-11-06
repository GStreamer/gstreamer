# please keep librfb easily extractable

noinst_LTLIBRARIES = librfb.la
plugin_LTLIBRARIES = libgstrfbsrc.la

libgstrfbsrc_la_SOURCES = gstrfbsrc.c
libgstrfbsrc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(X11_CFLAGS) -I$(srcdir)/..
libgstrfbsrc_la_LIBADD = $(GST_BASE_LIBS) $(X11_LIBS) librfb.la
libgstrfbsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrfbsrc_la_LIBTOOLFLAGS = --tag=disable-static

librfb_la_SOURCES = \
	rfbbuffer.c \
	rfbdecoder.c \
	d3des.c \
	vncauth.c
librfb_la_CFLAGS = $(GST_CFLAGS) -I$(srcdir)/..
librfb_la_LIBADD = $(GLIB_LIBS)

noinst_HEADERS = \
	rfb.h \
	rfbdecoder.h \
	rfbbuffer.h \
	rfbcontext.h \
	rfbutil.h \
	gstrfbsrc.h \
	d3des.h \
	vncauth.h
