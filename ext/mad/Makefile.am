filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstmad.la

libgstmad_la_SOURCES =	\
	gstmad.c	

libgstmad_la_CFLAGS = $(GST_CFLAGS)
libgstmad_la_LIBADD = -lmad

noinst_HEADERS = gstmad.h
