filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstriff.la

libgstriff_la_SOURCES = gstriffparse.c gstriffencode.c gstriffutil.c

libgstriffincludedir = $(includedir)/gst/libs/gstriff
libgstriffinclude_HEADERS = gstriff.h

CFLAGS += -O2 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math
