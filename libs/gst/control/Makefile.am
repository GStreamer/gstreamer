librarydir = $(libdir)/gst

library_LTLIBRARIES = libgstcontrol.la

libgstcontrolincludedir = $(includedir)/gstreamer-@VERSION@/gst/control
libgstcontrolinclude_HEADERS = \
    control.h \
    dparammanager.h \
    dparam.h \
    dparam_smooth.h \
    unitconvert.h \
    dparamcommon.h \
    dplinearinterp.h

libgstcontrol_la_SOURCES = \
    control.c \
    dparammanager.c \
    dparam.c \
    dparam_smooth.c \
    unitconvert.c \
    dplinearinterp.c

libgstcontrol_la_CFLAGS = $(GST_CFLAGS) -finline-functions -ffast-math
libgstcontrol_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
