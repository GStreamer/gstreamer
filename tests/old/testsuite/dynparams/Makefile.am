SUBDIRS =

testprogs = dparamstest

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

dparamstest_SOURCES = dparamstest.c
dparamstest_LDFLAGS = -L$(top_builddir)/libs/control/ -lgstcontrol
# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
CFLAGS += $(GST_CFLAGS)
