plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgsta52dec.la

libgsta52dec_la_SOURCES = gsta52dec.c
libgsta52dec_la_CFLAGS = $(GST_CFLAGS) $(FOMIT_FRAME_POINTER)
libgsta52dec_la_LIBADD = $(A52DEC_LIBS)
libgsta52dec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gsta52dec.h
