plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmpeg2dec.la

##if HAVE_CPU_I386
##ARCHFLAGS = -mcpu=i486
##else
ARCHFLAGS = 
##endif

libgstmpeg2dec_la_SOURCES = gstmpeg2dec.c
libgstmpeg2dec_la_CFLAGS = $(GST_CFLAGS) $(MPEG2DEC_CFLAGS) $(ARCHFLAGS) $(FOMIT_FRAME_POINTER) -ffast-math
libgstmpeg2dec_la_LIBADD = $(MPEG2DEC_LIBS)
libgstmpeg2dec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(MPEG2DEC_LIBS)

noinst_HEADERS = gstmpeg2dec.h
