
SUBDIRS = instantiate memchunk muxing sched threadstate seeking # bufspeed

if !GST_DISABLE_TRACE
noinst_PROGRAMS = lat

lat_CFLAGS = $(GST_OBJ_CFLAGS)
lat_LDFLAGS = $(GST_OBJ_LIBS)
endif

EXTRA_DIST = README
DIST_SUBDIRS= bufspeed instantiate memchunk muxing sched threadstate seeking
