filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstbytestream.la

libgstbytestream_la_SOURCES = gstbytestream.c 

libgstbytestreamincludedir = $(includedir)/gst/libs/bytestream
libgstbytestreaminclude_HEADERS = gstbytestream.h

CFLAGS += -O2 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math
