filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgsttypes.la

libgsttypes_la_SOURCES = gsttypes.c

libgsttypes_la_CFLAGS = $(XML_CFLAGS) $(GLIB_CFLAGS)
libgsttypes_la_LIBADD = $(XML_LIBS) $(GLIB_LIBS)
libgsttypes_la_LDFLAGS = -version-info $(GST_LIBVERSION)
