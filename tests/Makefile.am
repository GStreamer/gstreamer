# This directory depends on an x86 computer, so if nasm is present, we're good
# complain to hadess@hadess.net
# bufspeed gives warnings, can someone please fix that ?
NASMDEP_DIR=memchunk #bufspeed

SUBDIRS = $(NASMDEP_DIR) muxing sched threadstate seeking

noinst_PROGRAMS = lat

lat_CFLAGS = $(GST_CFLAGS)
lat_LDFLAGS = $(GST_LIBS)

EXTRA_DIST = README
DIST_SUBDIRS= bufspeed memchunk muxing sched threadstate seeking
