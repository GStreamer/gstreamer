librarydir = $(libdir)/gst

library_LTLIBRARIES = libgstcontrol.la

libgstcontrol_la_SOURCES = \
    control.c \
    dparammanager.c \
    dparam.c \
    dplinearinterp.c
    

libgstcontrolincludedir = $(includedir)/gst/control
libgstcontrolinclude_HEADERS = \
    control.h \
    dparammanager.h \
    dparam.h \
    dparamcommon.h \
    dplinearinterp.h

libgstcontrol_la_LIBADD = $(GST_LIBS)
libgstcontrol_la_CFLAGS = $(GST_CFLAGS) -finline-functions -ffast-math

