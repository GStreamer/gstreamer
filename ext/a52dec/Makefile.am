plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgsta52dec.la

if HAVE_CPU_I386
ARCHFLAGS = -m486
else
ARCHFLAGS = 
endif

libgsta52dec_la_SOURCES = gsta52dec.c
libgsta52dec_la_CFLAGS = $(GST_CFLAGS) -O3 $(ARCHFLAGS) $(FOMIT_FRAME_POINTER)
libgsta52dec_la_LIBADD = -la52 -lm
libgsta52dec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gsta52dec.h
