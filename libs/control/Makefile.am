filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstcontrol.la

libgstcontrol_la_SOURCES = \
    gstcontrol.c \
    gstdparammanager.c \
    gstdparam.c \
    gstdplinearinterp.c
    

libgstcontrolincludedir = $(includedir)/gst/libs/control
libgstcontrolinclude_HEADERS = \
    gstcontrol.h \
    gstdparammanager.h \
    gstdparam.h \
    gstdparamcommon.h \
    gstdplinearinterp.h

libgstcontrol_la_LIBADD = -lm
libgstcontrol_la_CFLAGS = -O2 $(FOMIT_FRAME_POINTER) -finline-functions -ffast-math $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)
