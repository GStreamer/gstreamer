testprogs = dparamstest

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

dparamstest_CFLAGS = $(GST_CFLAGS)
dparamstest_LDFLAGS = $(GST_LIBS) -L$(top_builddir)/libs/gst/control/ -lgstcontrol

dparamstest_SOURCES = dparamstest.c
