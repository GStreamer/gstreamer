librarydir = $(libdir)

library_LTLIBRARIES = libgstcontrol-@GST_MAJORMINOR@.la

libgstcontrol_@GST_MAJORMINOR@_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/control
libgstcontrol_@GST_MAJORMINOR@_include_HEADERS = \
    control.h \
    dparammanager.h \
    dparam.h \
    dparam_smooth.h \
    unitconvert.h \
    dparamcommon.h \
    dplinearinterp.h

libgstcontrol_@GST_MAJORMINOR@_la_SOURCES = \
    control.c \
    dparammanager.c \
    dparam.c \
    dparam_smooth.c \
    unitconvert.c \
    dplinearinterp.c

libgstcontrol_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS)
libgstcontrol_@GST_MAJORMINOR@_la_LDFLAGS = @GST_LT_LDFLAGS@ -version-info @GST_LIBVERSION@
# control uses log and exp, so we need -lm if xml is disabled
libgstcontrol_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) -lm

