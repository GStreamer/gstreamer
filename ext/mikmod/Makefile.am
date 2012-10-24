plugin_LTLIBRARIES = libgstmikmod.la

libgstmikmod_la_SOURCES = gstmikmod.c drv_gst.c mikmod_reader.c 
libgstmikmod_la_CFLAGS = $(GST_CFLAGS) $(MIKMOD_CFLAGS)
libgstmikmod_la_LIBADD = $(GST_LIBS) $(MIKMOD_LIBS)
libgstmikmod_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmikmod_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstmikmod.h 

EXTRA_DIST = README
