plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libv4lsrc.la

libv4lsrc_la_SOURCES =	\
                   gstv4lsrc.c \
                   grab-v4l.c

## FIXME : do we need -O2 ? libv4lsrc_la_CFLAGS = -O2 $(GST_CFLAGS)
libv4lsrc_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = gstv4lsrc.h grab.h
