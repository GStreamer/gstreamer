plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = \
	libgstbasicomegascheduler.la \
	libgstbasicgthreadscheduler.la \
	libgstentrygthreadscheduler.la \
	libgstentryomegascheduler.la \
	libgstfairgthreadscheduler.la \
	libgstoptscheduler.la \
	libgstoptomegascheduler.la \
	libgstoptgthreadscheduler.la

libgstbasicomegascheduler_la_SOURCES = gstbasicscheduler.c 
libgstbasicomegascheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_OMEGA
libgstbasicomegascheduler_la_LIBADD = ../libcothreads.la
libgstbasicomegascheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstbasicgthreadscheduler_la_SOURCES = gstbasicscheduler.c 
libgstbasicgthreadscheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_GTHREAD
libgstbasicgthreadscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstentrygthreadscheduler_la_SOURCES = entryscheduler.c 
libgstentrygthreadscheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_GTHREAD
libgstentrygthreadscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstentryomegascheduler_la_SOURCES = entryscheduler.c 
libgstentryomegascheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_OMEGA
libgstentryomegascheduler_la_LIBADD = ../libcothreads.la
libgstentryomegascheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstoptscheduler_la_SOURCES = gstoptimalscheduler.c 
libgstoptscheduler_la_CFLAGS = $(GST_CFLAGS) 
libgstoptscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstoptomegascheduler_la_SOURCES = gstoptimalscheduler.c 
libgstoptomegascheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_OMEGA -DUSE_COTHREADS
libgstoptomegascheduler_la_LIBADD = ../libcothreads.la
libgstoptomegascheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstoptgthreadscheduler_la_SOURCES = gstoptimalscheduler.c 
libgstoptgthreadscheduler_la_CFLAGS = $(GST_CFLAGS) -D_COTHREADS_GTHREAD -DUSE_COTHREADS
libgstoptgthreadscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = cothreads_compat.h gthread-cothreads.h
