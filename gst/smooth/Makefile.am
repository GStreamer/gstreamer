filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstsmooth.la

libgstsmooth_la_SOURCES = gstsmooth.c
libgstsmooth_la_CFLAGS = -O3 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math $(GST_CFLAGS)

noinst_HEADERS = gstsmooth.h
