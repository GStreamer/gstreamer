# please keep librfb easily extractable

plugin_LTLIBRARIES = libgstrfbsrc.la

libgstrfbsrc_la_SOURCES = gstrfbsrc.c \
	rfbdecoder.c \
	d3des.c
libgstrfbsrc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(X11_CFLAGS) -I$(srcdir)/..
libgstrfbsrc_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(X11_LIBS)
libgstrfbsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrfbsrc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	rfbdecoder.h \
	gstrfbsrc.h \
	d3des.h
