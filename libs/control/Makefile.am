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

libgstcontrol_la_LIBADD = $(GST_LIBS)
libgstcontrol_la_CFLAGS = $(GST_CFLAGS) -finline-functions -ffast-math

