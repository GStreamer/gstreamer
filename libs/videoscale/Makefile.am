filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstvideoscale.la

if HAVE_CPU_I386
SCALER = gstscale_x86.c gstscale_x86_asm.s
else
SCALER =
endif

libgstvideoscale_la_SOURCES = gstvideoscale.c $(SCALER)

libgstvideoscaleincludedir = $(includedir)/gst/libs/gstvideoscale
libgstvideoscaleinclude_HEADERS = gstvideoscale.h 

noinst_HEADERS = gstscale_x86.h

#CFLAGS += -S -O1 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math
CFLAGS = $(GLIB_CFLAGS) $(LIBGST_CFLAGS) $(XML_CFLAGS) -O5 -fomit-frame-pointer -ffast-math
