filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstcolorspace.la

if HAVE_CPU_I386
ARCHSRCS = yuv2rgb_mmx16.s
else
ARCHSRCS = 
endif

libgstcolorspace_la_SOURCES = gstcolorspace.c rgb2rgb.c yuv2rgb.c $(ARCHSRCS)

libgstcolorspaceincludedir = $(includedir)/gst/libs/gstcolorspace
libgstcolorspaceinclude_HEADERS = gstcolorspace.h

noinst_HEADERS = yuv2rgb.h

# FIXME is this needed?
CFLAGS += -O2 $(FOMIT_FRAME_POINTER) -finline-functions -ffast-math
