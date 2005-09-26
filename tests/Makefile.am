
SUBDIRS = instantiate muxing sched threadstate seeking

if GST_DISABLE_TRACE
LAT = 
else
LAT = lat

endif

noinst_PROGRAMS = $(LAT) mass_elements complexity

AM_CFLAGS = $(GST_OBJ_CFLAGS)
LIBS = $(GST_OBJ_LIBS) \
	$(top_builddir)/gst/base/libgstbase-@GST_MAJORMINOR@.la

EXTRA_DIST = README
DIST_SUBDIRS= instantiate muxing sched threadstate seeking
