plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmpeg2dec.la

if HAVE_CPU_I386
ARCHFLAGS = -m486
else
ARCHFLAGS = 
endif

libgstmpeg2dec_la_SOURCES = gstmpeg2dec.c
libgstmpeg2dec_la_CFLAGS = $(GST_CFLAGS) -O3 $(ARCHFLAGS) $(FOMIT_FRAME_POINTER) -ffast-math
libgstmpeg2dec_la_LIBADD = $(GST_LIBS) -lmpeg2 -lmpeg2dec

#
# Use the following if you use mpeg2dec from CVS
#
#libgstmpeg2dec_la_LIBADD = $(GST_LIBS) -lmpeg2 -lcpuaccel

noinst_HEADERS = gstmpeg2dec.h

#noinst_PROGRAMS = perftest

#perftest_SOURCES = perftest.c $(libgstmpeg2dec_la_SOURCES)
#perftest_CFLAGS = $(GST_CFLAGS)
#perftest_LDADD = $(GST_LIBS)

#CFLAGS += -pg -Wall -fprofile-arcs -ftest-coverage
#CFLAGS += -pg -Wall -O3
