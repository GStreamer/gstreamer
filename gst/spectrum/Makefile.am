filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstspectrum.la

libgstspectrum_la_SOURCES = gstspectrum.c fix_fft.c
libgstspectrum_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = gstspectrum.h

## this thing needs gnome too, so unless someone checks for it, let's 
## comment it out.
## if HAVE_GTK
## noinst_PROGRAMS = demo-osssrc
## endif

## demo_osssrc_SOURCES = demo-osssrc.c
## demo_osssrc_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
## demo_osssrc_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)

EXTRA_DIST = README
