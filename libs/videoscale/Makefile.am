filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstvideoscale.la

libgstvideoscale_la_SOURCES = gstvideoscale.c 

libgstvideoscaleincludedir = $(includedir)/gst/libs/gstvideoscale
libgstvideoscaleinclude_HEADERS = gstvideoscale.h

noinst_HEADERS = yuv2rgb.h

CFLAGS += -O1 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math
