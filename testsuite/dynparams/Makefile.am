include ../Rules

tests_pass = dparamstest
tests_fail =
tests_ignore =

dparamstest_CFLAGS = $(GST_OBJ_CFLAGS)
dparamstest_LDFLAGS = $(GST_OBJ_LIBS)
dparamstest_LDADD = $(top_builddir)/libs/gst/control/libgstcontrol-@GST_MAJORMINOR@.la

dparamstest_SOURCES = dparamstest.c
