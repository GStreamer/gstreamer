libgstinterfacesincludedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/xoverlay

libgstinterfacesinclude_HEADERS = xoverlay.h

noinst_LTLIBRARIES = libgstxoverlay.la

libgstxoverlay_la_SOURCES = xoverlay.c
libgstxoverlay_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstxoverlay_la_LIBADD = $(X_LIBS)

