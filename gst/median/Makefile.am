filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstmedian.la

libgstmedian_la_SOURCES = gstmedian.c

libgstmedian_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math $(GST_CFLAGS)

noinst_HEADERS = gstmedian.h
