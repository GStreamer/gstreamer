
plugin_LTLIBRARIES = libgstcdio.la

libgstcdio_la_SOURCES = \
	gstcdio.c \
	gstcdiocddasrc.c

libgstcdio_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(CDIO_CFLAGS) \
	$(GST_CFLAGS)

libgstcdio_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstcdda-@GST_MAJORMINOR@ \
	$(CDIO_LIBS)

libgstcdio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 

noinst_HEADERS = \
	gstcdio.h \
	gstcdiocddasrc.h

