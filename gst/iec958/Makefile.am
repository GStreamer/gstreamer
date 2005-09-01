plugin_LTLIBRARIES = libiec958.la

libiec958_la_SOURCES = ac3iec.c ac3_padder.c
libiec958_la_CFLAGS = $(GLIB_CFLAGS) $(GST_CFLAGS) \
    $(GSTLIBS_CFLAGS) $(EXTRA_CFLAGS)
libiec958_la_LIBADD = $(GLIB_LIBS) $(GST_LIBS) $(GSTLIBS_LIBS)
libiec958_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = ac3_padder.h ac3iec.h 
