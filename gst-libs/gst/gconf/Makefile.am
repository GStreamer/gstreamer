librarydir = $(libdir)

library_LTLIBRARIES = libgstgconf-@GST_MAJORMINOR@.la

libgstgconf_@GST_MAJORMINOR@_la_SOURCES = gconf.c

libgstgconf_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/gconf
libgstgconf_@GST_MAJORMINOR@include_HEADERS = gconf.h

noinst_PROGRAMS = test-gconf

test_gconf_CFLAGS = $(GST_CFLAGS) $(GCONF_CFLAGS)
test_gconf_LDADD = $(GST_LIBS) $(GCONF_LIBS) libgstgconf-@GST_MAJORMINOR@.la

libgstgconf_@GST_MAJORMINOR@_la_LIBADD = $(GCONF_LIBS)
libgstgconf_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) $(GCONF_CFLAGS)
libgstgconf_@GST_MAJORMINOR@_la_LDFLAGS = @GST_PLUGINS_LT_LDFLAGS@ -version-info @GST_PLUGINS_LIBVERSION@
