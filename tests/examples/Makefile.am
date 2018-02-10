if USE_GTK3
GTK_DIR=gtk
else
GTK_DIR=
endif

if USE_JACK
JACK_DIR=jack
else
JACK_DIR=
endif

if USE_CAIRO
CAIRO_DIR=cairo
else
CAIRO_DIR=
endif

SUBDIRS = audiofx equalizer $(GTK_DIR) $(JACK_DIR) level \
	rtp shapewipe spectrum v4l2 $(CAIRO_DIR)

DIST_SUBDIRS = audiofx equalizer gtk jack level \
	rtp shapewipe spectrum v4l2 cairo

include $(top_srcdir)/common/parallel-subdirs.mak
