plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstbasicscheduler.la libgstpthscheduler.la

libgstbasicscheduler_la_SOURCES = gstbasicscheduler.c 
libgstbasicscheduler_la_CFLAGS = $(GST_CFLAGS)
libgstbasicscheduler_la_LIBADD = $(GST_LIBS)
libgstbasicscheduler_la_LDFLAGS = -version-info $(GST_LIBVERSION)

libgstpthscheduler_la_SOURCES = gstpthscheduler.c 
libgstpthscheduler_la_CFLAGS = $(GST_CFLAGS)
libgstpthscheduler_la_LIBADD = $(GST_LIBS) -lpth
libgstpthscheduler_la_LDFLAGS = -version-info $(GST_LIBVERSION)
