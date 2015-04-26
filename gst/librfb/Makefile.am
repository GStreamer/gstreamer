# please keep librfb easily extractable

noinst_LTLIBRARIES = librfb.la
plugin_LTLIBRARIES = libgstrfbsrc.la

libgstrfbsrc_la_SOURCES = gstrfbsrc.c
libgstrfbsrc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(X11_CFLAGS) -I$(srcdir)/..
libgstrfbsrc_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(X11_LIBS) librfb.la
libgstrfbsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrfbsrc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

librfb_la_SOURCES = \
	rfbbuffer.c \
	rfbdecoder.c \
	d3des.c
librfb_la_CFLAGS = $(GST_CFLAGS) $(GIO_CFLAGS) -I$(srcdir)/..
librfb_la_LIBADD = $(GST_LIBS) $(GIO_LIBS)

noinst_HEADERS = \
	rfb.h \
	rfbdecoder.h \
	rfbbuffer.h \
	rfbcontext.h \
	rfbutil.h \
	gstrfbsrc.h \
	d3des.h
