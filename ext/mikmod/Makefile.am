plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmikmod.la

libgstmikmod_la_SOURCES = gstmikmod.c drv_gst.c mikmod_reader.c mikmod_types.c

libgstmikmod_la_LIBADD = $(MIKMOD_LIBS)
libgstmikmod_la_CFLAGS = $(MIKMOD_CFLAGS) $(GST_CFLAGS)

noinst_HEADERS = gstmikmod.h

EXTRA_DIST = README
