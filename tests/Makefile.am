# This directory depends on an x86 computer, so if nasm is present, we're good
# complain to hadess@hadess.net
if HAVE_NASM
NASMDEP_DIR=bufspeed memchunk
else
NASMDEP_DIR=
endif

SUBDIRS = $(NASMDEP_DIR) muxing sched

noinst_PROGRAMS = lat

lat_CFLAGS = $(GST_CFLAGS)
lat_LDFLAGS = $(GST_LIBS)

EXTRA_DIST = README
DIST_SUBDIRS= bufspeed memchunk muxing sched
