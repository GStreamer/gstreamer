## libdir = $(libdir)/gst

lib_LTLIBRARIES = libgstresample.la

if HAVE_CPU_I386
ARCHCFLAGS = -march=i486
else
if HAVE_CPU_PPC
ARCHCFLAGS = -Wa,-m7400
else
ARCHCFLAGS =
endif
endif

libgstresample_la_SOURCES = dtos.c functable.c resample.c resample.h
libgstresample_la_LIBADD = $(GST_LIBS)
libgstresample_la_CFLAGS = $(GST_CFLAGS) -ffast-math $(ARCHCFLAGS)

noinst_HEADERS = resample.h

#check_PROGRAMS = test
#test_SOURCES = test.c
#test_LDADD = libresample.la



