filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstcontrol.la

libgstcontrol_la_SOURCES = gstdplinearinterp.c

libgstcontrolincludedir = $(includedir)/gst/libs/control
libgstcontrolinclude_HEADERS = gstcontrol.h gstdplinearinterp.h

CFLAGS += -O2 $(FOMIT_FRAME_POINTER) -finline-functions -ffast-math
