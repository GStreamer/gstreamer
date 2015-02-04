plugin_LTLIBRARIES = libgstsctp.la

libgstsctp_la_SOURCES = \
    gstsctpplugin.c \
    sctpassociation.c \
    gstsctpenc.c \
    gstsctpdec.c

libgstsctp_la_CFLAGS = \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_BASE_CFLAGS) \
    $(GST_CFLAGS) \
    $(USRSCTP_CFLAGS) \
    -I$(top_srcdir)/gst-libs

libgstsctp_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(USRSCTP_LIBS) $(top_builddir)/gst-libs/gst/sctp/libgstsctp-1.0.la

noinst_HEADERS = \
    sctpassociation.h \
    gstsctpenc.h \
    gstsctpdec.h
