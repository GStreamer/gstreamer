
SUBDIRS = instantiate memchunk muxing sched threadstate seeking # bufspeed

if GST_DISABLE_TRACE
LAT = 
else
LAT = lat

endif

noinst_PROGRAMS = $(LAT) spidey_bench

AM_CFLAGS = $(GST_OBJ_CFLAGS)
LIBS = $(GST_OBJ_LIBS)

EXTRA_DIST = README
DIST_SUBDIRS= bufspeed instantiate memchunk muxing sched threadstate seeking
