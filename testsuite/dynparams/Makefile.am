include ../Rules

tests_pass =
tests_fail = dparamstest 
tests_ignore =

dparamstest_CFLAGS = $(GST_CFLAGS)
dparamstest_LDFLAGS = $(GST_LIBS)
dparamstest_LDADD = $(top_builddir)/libs/gst/control/libgstcontrol-@GST_MAJORMINOR@.la

dparamstest_SOURCES = dparamstest.c
