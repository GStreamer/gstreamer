plugin_LTLIBRARIES = libgstmimic.la

libgstmimic_la_SOURCES = gstmimic.c gstmimdec.c gstmimenc.c

libgstmimic_la_CFLAGS = $(GST_CFLAGS) $(MIMIC_CFLAGS) $(GST_BASE_CFLAGS) $(ERROR_CFLAGS)
libgstmimic_la_LIBADD = $(GST_LIBS) $(MIMIC_LIBS) 
libgstmimic_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS)

noinst_HEADERS = gstmimdec.h gstmimenc.h
