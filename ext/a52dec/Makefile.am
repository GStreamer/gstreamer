filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgsta52dec.la

libgsta52dec_la_SOURCES = gsta52dec.c
libgsta52dec_la_LIBADD = -la52 -lm
libgsta52dec_la_CFLAGS = -O3 $(ARCHFLAGS) $(FOMIT_FRAME_POINTER) $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)

noinst_HEADERS = gsta52dec.h

if HAVE_CPU_I386
ARCHFLAGS = -m486
else
ARCHFLAGS = 
endif

