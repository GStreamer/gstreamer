plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstgsm.la

libgstgsm_la_SOURCES = gstgsm.c gstgsmdec.c gstgsmenc.c
libgstgsm_la_LIBADD = $(GSM_LIBS)
libgstgsm_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = gstgsmenc.h gstgsmdec.h 

#check_PROGRAMS = test

#test_CFLAGS = $(GSM_CFLAGS)
#test_LDADD = $(GSM_LIBS) $(top_srcdir)/gst/libgst.la
#test_SOURCES = test.c
