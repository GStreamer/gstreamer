plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmikmod.la

libgstmikmod_la_SOURCES = gstmikmod.c drv_gst.c mikmod_reader.c mikmod_types.c
libgstmikmod_la_CFLAGS = $(GST_CFLAGS) $(MIKMOD_CFLAGS)
libgstmikmod_la_LIBADD = $(GST_LIBS) $(MIKMOD_LIBS)

noinst_HEADERS = gstmikmod.h

EXTRA_DIST = README
