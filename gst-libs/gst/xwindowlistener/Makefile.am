plugin_LTLIBRARIES = libgstxwindowlistener.la

libgstxwindowlistener_la_SOURCES = xwindowlistener.c

libraryincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/xwindowlistener
libraryinclude_HEADERS = xwindowlistener.h

libgstxwindowlistener_la_LIBADD =$(GST_LIBS) $(GST_PLUGIN_LIBS) $(X_LIBS)
libgstxwindowlistener_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS)
libgstxwindowlistener_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
