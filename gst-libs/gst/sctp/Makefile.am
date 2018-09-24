lib_LTLIBRARIES = libgstsctp-1.0.la

libgstsctp_1_0_la_SOURCES = \
    sctpsendmeta.c \
    sctpreceivemeta.c

libgstsctp_1_0_la_CFLAGS = \
    -I$(top_srcdir)/gst-libs \
    -I$(top_builddir)/gst-libs \
    -DBUILDING_GST_SCTP \
    $(GST_PLUGINS_BASE_CFLAGS) \
    $(GST_BASE_CFLAGS) \
    $(GST_CFLAGS)

libgstsctp_1_0_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS)

libgstsctp_1_0_includedir = $(includedir)/gstreamer-1.0/gst/sctp
libgstsctp_1_0_include_HEADERS = \
    sctpsendmeta.h \
    sctpreceivemeta.h \
    sctp-prelude.h
