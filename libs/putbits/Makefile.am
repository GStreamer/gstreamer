filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstputbits.la

libgstputbits_la_SOURCES = gstputbits.c 

libgstputbitsincludedir = $(includedir)/gst/libs/gstputbits
libgstputbitsinclude_HEADERS = gstputbits.h

noinst_HEADERS = gstputbits.h 

CFLAGS += -O2 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math
