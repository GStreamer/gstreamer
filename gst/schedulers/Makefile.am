plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstbasicscheduler.la libgststandardscheduler.la libgstfastscheduler.la

libgstbasicscheduler_la_SOURCES = gstbasicscheduler.c 
libgstbasicscheduler_la_CFLAGS = $(GST_CFLAGS)
libgstbasicscheduler_la_LIBADD = ../libcothreads.la
libgstbasicscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgststandardscheduler_la_SOURCES = gststandardscheduler.c 
libgststandardscheduler_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/libs/ext/cothreads
libgststandardscheduler_la_LIBADD = $(top_builddir)/libs/ext/cothreads/cothreads/libcothreads-gthreads.la
libgststandardscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstfastscheduler_la_SOURCES = gstfastscheduler.c 
libgstfastscheduler_la_CFLAGS = $(GST_CFLAGS)
libgstfastscheduler_la_LIBADD = $(GST_LIBS) ../libcothreads.la
libgstfastscheduler_la_LDFLAGS = @GST_LT_LDFLAGS@

## this is a REALLY evil hack 
## but we need to keep it as long as we have libs/gst and libs/ext
$(top_builddir)/libs/ext/cothreads/cothreads/libcothreads-gthreads.la:
	cd $(top_builddir)/libs/ext/cothreads/cothreads && make

