librarydir = $(libdir)

library_LTLIBRARIES = libgstgconf-@GST_MAJORMINOR@.la

libgstgconf_@GST_MAJORMINOR@_la_SOURCES = gconf.c

libgstgconf_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/gconf
libgstgconf_@GST_MAJORMINOR@include_HEADERS = gconf.h

noinst_PROGRAMS = test-gconf

# add define for GST_GCONF_DIR
DIR_CFLAGS=-DGST_GCONF_DIR=\"/system/gstreamer/@GST_MAJORMINOR@\"

test_gconf_CFLAGS = $(GST_CFLAGS) $(GCONF_CFLAGS) $(DIR_CFLAGS)
test_gconf_LDADD = $(GST_LIBS) $(GCONF_LIBS) libgstgconf-@GST_MAJORMINOR@.la

libgstgconf_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) $(GCONF_LIBS)
libgstgconf_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) $(GCONF_CFLAGS) $(DIR_CFLAGS)
libgstgconf_@GST_MAJORMINOR@_la_LDFLAGS = @GST_PLUGINS_LT_LDFLAGS@ -version-info @GST_PLUGINS_LIBVERSION@

EXTRA_DIST = gstreamer-gconf.pc.in gstreamer-gconf-uninstalled.pc.in
