plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstbasicscheduler.la libgststandardscheduler.la libgstfastbasicscheduler.la libgstfaststandardscheduler.la

libgstbasicscheduler_la_SOURCES = gstbasicscheduler.c 
libgstbasicscheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_BASIC
libgstbasicscheduler_la_LIBADD = ../libcothreads.la
libgstbasicscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgststandardscheduler_la_SOURCES = gstbasicscheduler.c
libgststandardscheduler_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/libs/ext/cothreads -D_COTHREADS_STANDARD
libgststandardscheduler_la_LIBADD = $(top_builddir)/libs/ext/cothreads/cothreads/libcothreads-gthreads.la
libgststandardscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstfastbasicscheduler_la_SOURCES = gstfastscheduler.c 
libgstfastbasicscheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_BASIC
libgstfastbasicscheduler_la_LIBADD = ../libcothreads.la
libgstfastbasicscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstfaststandardscheduler_la_SOURCES = gstfastscheduler.c
libgstfaststandardscheduler_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/libs/ext/cothreads -D_COTHREADS_STANDARD
libgstfaststandardscheduler_la_LIBADD = $(top_builddir)/libs/ext/cothreads/cothreads/libcothreads-gthreads.la
libgstfaststandardscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

## this is a REALLY evil hack 
## but we need to keep it as long as we have libs/gst and libs/ext
$(top_builddir)/libs/ext/cothreads/cothreads/libcothreads-gthreads.la:
	cd $(top_builddir)/libs/ext/cothreads/cothreads && make

EXTRA_DIST = cothreads_compat.h
