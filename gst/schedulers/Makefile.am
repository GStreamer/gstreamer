plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstbasicscheduler.la libgststandardscheduler.la

libgstbasicscheduler_la_SOURCES = gstbasicscheduler.c 
libgstbasicscheduler_la_CFLAGS = $(GST_CFLAGS)
libgstbasicscheduler_la_LIBADD = $(GST_LIBS) ../libcothreads.la
libgstbasicscheduler_la_LDFLAGS = @GST_LT_LDFLAGS@

## this is a REALLY evil hack which we need to get rid of as soon as possible !
$(top_builddir)/libs/ext/cothreads/cothreads/libcothreads-gthreads.la:
	cd $(top_builddir)/libs/ext/cothreads/cothreads && make

libgststandardscheduler_la_SOURCES = gststandardscheduler.c 
libgststandardscheduler_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/libs/ext/cothreads
libgststandardscheduler_la_LIBADD = $(GST_LIBS) $(top_builddir)/libs/ext/cothreads/cothreads/libcothreads-gthreads.la
libgststandardscheduler_la_LDFLAGS = @GST_LT_LDFLAGS@
