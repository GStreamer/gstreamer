plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = \
	libgstbasicomegascheduler.la \
	libgstbasicwingoscheduler.la \
	libgstoptscheduler.la \
	libgstoptomegascheduler.la \
	libgstoptwingoscheduler.la

libgstbasicomegascheduler_la_SOURCES = gstbasicscheduler.c 
libgstbasicomegascheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_OMEGA
libgstbasicomegascheduler_la_LIBADD = ../libcothreads.la
libgstbasicomegascheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# some silly bug prevents us from putting both $(top_builddir) and
# $(src_dir) on the same line for CFLAGS (try it, reverse them, see the
# strangeness on your gcc line) so we pull this dirty += trick on it, hah !
libgstbasicwingoscheduler_la_SOURCES = gstbasicscheduler.c
libgstbasicwingoscheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_WINGO
libgstbasicwingoscheduler_la_CFLAGS += -I$(top_builddir)/libs/ext/cothreads
libgstbasicwingoscheduler_la_CFLAGS += -I$(top_srcdir)/libs/ext/cothreads
libgstbasicwingoscheduler_la_LIBADD = $(top_builddir)/libs/ext/cothreads/cothreads/libcothreads-gthreads.la
libgstbasicwingoscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstoptscheduler_la_SOURCES = gstoptimalscheduler.c 
libgstoptscheduler_la_CFLAGS = $(GST_CFLAGS) 
libgstoptscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstoptomegascheduler_la_SOURCES = gstoptimalscheduler.c 
libgstoptomegascheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_OMEGA -DUSE_COTHREADS
libgstoptomegascheduler_la_LIBADD = ../libcothreads.la
libgstoptomegascheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstoptwingoscheduler_la_SOURCES = gstoptimalscheduler.c 
libgstoptwingoscheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_WINGO -DUSE_COTHREADS
libgstoptwingoscheduler_la_CFLAGS += -I$(top_builddir)/libs/ext/cothreads
libgstoptwingoscheduler_la_CFLAGS += -I$(top_srcdir)/libs/ext/cothreads
libgstoptwingoscheduler_la_LIBADD = $(top_builddir)/libs/ext/cothreads/cothreads/libcothreads-gthreads.la
libgstoptwingoscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

## this is a REALLY evil hack 
## but we need to keep it as long as we have libs/gst and libs/ext
$(top_builddir)/libs/ext/cothreads/cothreads/libcothreads-gthreads.la:
	cd $(top_builddir)/libs/ext/cothreads/cothreads && ${MAKE}

noinst_HEADERS = cothreads_compat.h
