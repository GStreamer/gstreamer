
filterdir = $(libdir)/gst

filter_LTLIBRARIES = libresample.la


libresample_la_SOURCES = dtos.c functable.c resample.c resample.h


noinst_HEADERS = resample.h

check_PROGRAMS = test

test_SOURCES = test.c
test_LDADD = libresample.la


CFLAGS += -O2 -ffast-math

