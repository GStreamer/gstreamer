plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstmpeg2subt.la

libgstmpeg2subt_la_SOURCES = gstmpeg2subt.c  

if HAVE_CPU_I386
ARCHCFLAGS = -mcpu=i486
else
ARCHCFLAGS = 
endif

libgstmpeg2subt_la_CFLAGS = -O3 $(ARCHCFLAGS) $(GST_FSCHEDULE_CFLAGS) $(FOMIT_FRAME_POINTER) $(GST_OPT_CFLAGS) $(GST_CFLAGS)
libgstmpeg2subt_la_LIBADD =
libgstmpeg2subt_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmpeg2subt.h 

EXTRA_DIST = Notes.txt
