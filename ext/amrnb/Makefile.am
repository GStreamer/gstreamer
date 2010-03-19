plugin_LTLIBRARIES = libgstamrnb.la

libgstamrnb_la_SOURCES = \
	amrnb.c \
	amrnbdec.c \
	amrnbenc.c

libgstamrnb_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AMRNB_CFLAGS)
libgstamrnb_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(AMRNB_LIBS)
libgstamrnb_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstamrnb_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	amrnbdec.h \
	amrnbenc.h

presetdir = $(datadir)/gstreamer-$(GST_MAJORMINOR)/presets
preset_DATA = GstAmrnbEnc.prs

EXTRA_DIST = $(preset_DATA)
