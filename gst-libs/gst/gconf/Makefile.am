librarydir = $(prefix)/lib/gst

library_LTLIBRARIES = libgstgconf.la

libgstgconf_la_SOURCES = gconf.c

libgstgconfincludedir = $(includedir)/@PACKAGE@-@VERSION@/gst/gconf
libgstgconfinclude_HEADERS = gconf.h

noinst_PROGRAMS = test-gconf

test_gconf_CFLAGS = $(GST_CFLAGS) $(GCONF_CFLAGS)
test_gconf_LDADD = $(GST_LIBS) $(GCONF_LIBS) -lgstgconf

libgstgconf_la_LIBADD = $(GCONF_LIBS)
libgstgconf_la_CFLAGS = $(GST_CFLAGS) $(GCONF_CFLAGS)
libgstgconf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
