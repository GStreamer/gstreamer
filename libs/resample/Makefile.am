
filterdir = $(libdir)/gst

filter_LTLIBRARIES = libresample.la


libresample_la_SOURCES = dtos.c functable.c resample.c resample.h


noinst_HEADERS = resample.h

#check_PROGRAMS = test

#test_SOURCES = test.c
#test_LDADD = libresample.la


if HAVE_CPU_I386
ARCHCFLAGS = -march=i486
else
if HAVE_CPU_PPC
ARCHCFLAGS = -Wa,-m7400
else
ARCHCFLAGS =
endif
endif

libresample_la_LIBADD = $(GST_LIBS)
libresample_la_CFLAGS = $(GST_CFLAGS) -ffast-math $(ARCHCFLAGS)
