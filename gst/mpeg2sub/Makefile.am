plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmpeg2subt.la

libgstmpeg2subt_la_SOURCES = gstmpeg2subt.c  

if HAVE_CPU_I386
ARCHCFLAGS = -m486
else
ARCHCFLAGS = 
endif

libgstmpeg2subt_la_CFLAGS = -O3 $(ARCHCFLAGS) -fschedule-insns2 $(FOMIT_FRAME_POINTER) -finline-functions -ffast-math $(GST_CFLAGS)
libgstmpeg2subt_la_LIBADD = $(GST_LIBS)
libgstmpeg2subt_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmpeg2subt.h 

EXTRA_DIST = Notes.txt
