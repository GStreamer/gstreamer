
plugin_LTLIBRARIES = libgstspectrum.la

libgstspectrum_la_SOURCES = gstspectrum.c fix_fft.c
libgstspectrum_la_CFLAGS = $(GST_CFLAGS)
libgstspectrum_la_LIBADD =
libgstspectrum_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstspectrum.h

if HAVE_GTK
noinst_PROGRAMS = demo-osssrc
endif

demo_osssrc_SOURCES = demo-osssrc.c
demo_osssrc_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
demo_osssrc_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)

EXTRA_DIST = README
