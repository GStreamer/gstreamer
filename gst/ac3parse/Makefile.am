filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstac3parse.la

libgstac3parse_la_SOURCES = gstac3parse.c

noinst_HEADERS = gstac3parse.h

libgstac3parse_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math $(GST_CFLAGS)

EXTRA_DIST = README
