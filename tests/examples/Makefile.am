if USE_JACK
JACK_DIR=jack
else
JACK_DIR=
endif

SUBDIRS = audiofx equalizer $(JACK_DIR) level pulse rtp shapewipe spectrum v4l2

DIST_SUBDIRS = audiofx equalizer jack level pulse rtp shapewipe spectrum v4l2

include $(top_srcdir)/common/parallel-subdirs.mak
