
lib_LTLIBRARIES = libgstcontrol-@GST_MAJORMINOR@.la
AS_LIBTOOL_LIB = libgstcontrol-@GST_MAJORMINOR@

EXTRA_DIST = $(as_libtool_EXTRA_DIST)
noinst_DATA = $(as_libtool_noinst_DATA_files)

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
libgstcontrol_@GST_MAJORMINOR@_la_LDFLAGS = @GST_LT_LDFLAGS@ -version-info @GST_LIBVERSION@ $(as_libtool_LDFLAGS)
# control uses log and exp, so we need -lm if xml is disabled
libgstcontrol_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) -lm

install-data-local: as-libtool-install-data-local

uninstall-local: as-libtool-uninstall-local

include $(top_srcdir)/common/as-libtool.mak

