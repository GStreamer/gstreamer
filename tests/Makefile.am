
SUBDIRS = instantiate sched threadstate seeking

noinst_PROGRAMS = mass_elements complexity

AM_CFLAGS = $(GST_OBJ_CFLAGS)
LIBS = $(GST_OBJ_LIBS) \
	$(top_builddir)/gst/base/libgstbase-@GST_MAJORMINOR@.la

EXTRA_DIST = README
DIST_SUBDIRS= instantiate sched threadstate seeking
