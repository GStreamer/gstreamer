plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstrtp.la

libgstrtp_la_SOURCES = gstrtp.c gstrtpL16enc.c gstrtpL16parse.c rtp-packet.c

libgstrtp_la_CFLAGS = $(GST_CFLAGS)
libgstrtp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstrtpL16enc.h gstrtpL16parse.h gstrtp-common.h rtp-packet.h
